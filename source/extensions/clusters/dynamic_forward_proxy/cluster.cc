#include "source/extensions/clusters/dynamic_forward_proxy/cluster.h"

#include <algorithm>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.validate.h"
#include "envoy/router/string_accessor.h"

#include "source/common/http/utility.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"
#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"
#include "source/extensions/transport_sockets/tls/utility.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicForwardProxy {

Cluster::Cluster(
    Server::Configuration::ServerFactoryContext& server_context,
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig& config,
    Upstream::ClusterFactoryContext& context, Runtime::Loader& runtime,
    Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
    const LocalInfo::LocalInfo& local_info, bool added_via_api)
    : Upstream::BaseDynamicClusterImpl(server_context, cluster, context, runtime, added_via_api,
                                       context.mainThreadDispatcher().timeSource()),
      dns_cache_manager_(cache_manager_factory.get()),
      dns_cache_(dns_cache_manager_->getCache(config.dns_cache_config())),
      update_callbacks_handle_(dns_cache_->addUpdateCallbacks(*this)), local_info_(local_info),
      main_thread_dispatcher_(server_context.mainThreadDispatcher()),
      refresh_interval_(
          PROTOBUF_GET_MS_OR_DEFAULT(config.dns_cache_config(), dns_refresh_rate, 60000)),
      host_ttl_(PROTOBUF_GET_MS_OR_DEFAULT(config.dns_cache_config(), host_ttl, 300000)),
      orig_cluster_config_(cluster), orig_dfp_config_(config),
      allow_coalesced_connections_(config.allow_coalesced_connections()),
      cm_(context.clusterManager()),
      enable_strict_dns_cluster_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.enable_strict_dns_sub_cluster_for_dfp_cluster")) {}

void Cluster::startPreInit() {
  // If we are attaching to a pre-populated cache we need to initialize our hosts.
  std::unique_ptr<Upstream::HostVector> hosts_added;
  dns_cache_->iterateHostMap(
      [&](absl::string_view host, const Common::DynamicForwardProxy::DnsHostInfoSharedPtr& info) {
        addOrUpdateHost(host, info, hosts_added);
      });
  if (hosts_added) {
    updatePriorityState(*hosts_added, {});
  }
  onPreInitComplete();
}

envoy::config::cluster::v3::Cluster Cluster::subClusterConfig(const std::string& cluster_name,
                                                              const std::string& host,
                                                              const int port) const {
  envoy::config::cluster::v3::Cluster config = orig_cluster_config_;

  config.set_dns_lookup_family(orig_dfp_config_.dns_cache_config().dns_lookup_family());

  // overwrite to a strict_dns cluster.
  config.set_name(cluster_name);
  config.clear_cluster_type();
  config.set_type(
      envoy::config::cluster::v3::Cluster_DiscoveryType::Cluster_DiscoveryType_STRICT_DNS);
  config.set_lb_policy(envoy::config::cluster::v3::Cluster_LbPolicy::Cluster_LbPolicy_ROUND_ROBIN);

  /*
    config.set_dns_lookup_family(
        envoy::config::cluster::v3::Cluster_DnsLookupFamily::Cluster_DnsLookupFamily_V4_ONLY);
    config.mutable_connect_timeout()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(parent_info->connectTimeout().count()));
  */

  auto load_assignments = config.mutable_load_assignment();
  load_assignments->set_cluster_name(cluster_name);
  load_assignments->clear_endpoints();

  auto socket_address = load_assignments->add_endpoints()
                            ->add_lb_endpoints()
                            ->mutable_endpoint()
                            ->mutable_address()
                            ->mutable_socket_address();
  socket_address->set_address(host);
  socket_address->set_port_value(port);

  return config;
}

Upstream::HostConstSharedPtr Cluster::chooseHost(absl::string_view host,
                                                 Upstream::LoadBalancerContext* context) {
  uint16_t default_port = 80;
  if (info_->transportSocketMatcher().resolve(nullptr).factory_.implementsSecureTransport()) {
    default_port = 443;
  }

  const auto host_attributes = Http::Utility::parseAuthority(host);
  auto dynamic_host = std::string(host_attributes.host_);
  auto port = host_attributes.port_.value_or(default_port);

  // cluster name is prefix + host + port
  auto dynamic_cluster_name = "DFPCluster:" + dynamic_host + ":" + std::to_string(port);

  // try again to get the real cluster.
  auto dynamic_cluster = cm_.getThreadLocalCluster(dynamic_cluster_name);
  if (dynamic_cluster == nullptr) {
    return nullptr;
  }

  auto chosen_host = dynamic_cluster->loadBalancer().chooseHost(context);
  if (chosen_host == nullptr) {
    return nullptr;
  }

  // TODO: check cluster in cluster_map.
  // 1. if cluster not existing, add to cluster map, and create a idle timeout timer.
  // 2. if cluster existing, update the last active time.
  {
    absl::ReaderMutexLock lock{&cluster_map_lock_};
    const auto cluster_it = cluster_map_.find(dynamic_cluster_name);
    if (cluster_it != cluster_map_.end()) {
      cluster_it->second->touch();
      return chosen_host;
    }
  }
  {
    absl::WriterMutexLock lock{&cluster_map_lock_};
    const auto cluster_it = cluster_map_.find(dynamic_cluster_name);
    if (cluster_it == cluster_map_.end()) {
      cluster_map_.emplace(dynamic_cluster_name,
                           std::make_shared<ClusterInfo>(dynamic_cluster_name, *this));
    } else {
      cluster_it->second->touch();
    }
  }
  return chosen_host;
}

Cluster::ClusterInfo::ClusterInfo(std::string cluster_name, Cluster& parent)
    : cluster_name_(cluster_name), parent_(parent) {
  ENVOY_LOG(debug, "cluster='{}' created, enable TTL check", cluster_name_);
  parent_.main_thread_dispatcher_.post([this]() {
    idle_timer_ = parent_.main_thread_dispatcher_.createTimer([this]() { checkIdle(); });
    std::chrono::seconds dns_ttl =
        std::chrono::duration_cast<std::chrono::seconds>(parent_.refresh_interval_);
    idle_timer_->enableTimer(std::chrono::milliseconds(dns_ttl));
  });
  touch();
}

void Cluster::ClusterInfo::touch() {
  last_used_time_ = parent_.time_source_.monotonicTime().time_since_epoch();
}

// checkIdle run in the main thread.
void Cluster::ClusterInfo::checkIdle() {
  ASSERT(parent_.main_thread_dispatcher_.isThreadSafe());

  const std::chrono::steady_clock::duration now_duration =
      parent_.main_thread_dispatcher_.timeSource().monotonicTime().time_since_epoch();
  auto last_used_time = last_used_time_.load();
  ENVOY_LOG(debug, "cluster='{}' TTL check: now={} last_used={} TTL {}", cluster_name_,
            now_duration.count(), last_used_time.count(), parent_.host_ttl_.count());

  if ((now_duration - last_used_time) > parent_.host_ttl_) {
    ENVOY_LOG(debug, "cluster='{}' TTL expired, removing in main thread", cluster_name_);
    parent_.cm_.removeCluster(cluster_name_);
  } else {
    std::chrono::seconds dns_ttl =
        std::chrono::duration_cast<std::chrono::seconds>(parent_.refresh_interval_);
    idle_timer_->enableTimer(std::chrono::milliseconds(dns_ttl));
  }
}

void Cluster::addOrUpdateHost(
    absl::string_view host,
    const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr& host_info,
    std::unique_ptr<Upstream::HostVector>& hosts_added) {
  Upstream::LogicalHostSharedPtr emplaced_host;
  {
    absl::WriterMutexLock lock{&host_map_lock_};

    // NOTE: Right now we allow a DNS cache to be shared between multiple clusters. Though we have
    // connection/request circuit breakers on the cluster, we don't have any way to control the
    // maximum hosts on a cluster. We currently assume that host data shared via shared pointer is
    // a marginal memory cost above that already used by connections and requests, so relying on
    // connection/request circuit breakers is sufficient. We may have to revisit this in the
    // future.
    const auto host_map_it = host_map_.find(host);
    if (host_map_it != host_map_.end()) {
      // If we only have an address change, we can do that swap inline without any other updates.
      // The appropriate R/W locking is in place to allow this. The details of this locking are:
      //  - Hosts are not thread local, they are global.
      //  - We take a read lock when reading the address and a write lock when changing it.
      //  - Address updates are very rare.
      //  - Address reads are only done when a connection is being made and a "real" host
      //    description is created or the host is queried via the admin endpoint. Both of
      //    these operations are relatively rare and the read lock is held for a short period
      //    of time.
      //
      // TODO(mattklein123): Right now the dynamic forward proxy / DNS cache works similar to how
      //                     logical DNS works, meaning that we only store a single address per
      //                     resolution. It would not be difficult to also expose strict DNS
      //                     semantics, meaning the cache would expose multiple addresses and the
      //                     cluster would create multiple logical hosts based on those addresses.
      //                     We will leave this is a follow up depending on need.
      ASSERT(host_info == host_map_it->second.shared_host_info_);
      ASSERT(host_map_it->second.shared_host_info_->address() !=
             host_map_it->second.logical_host_->address());
      ENVOY_LOG(debug, "updating dfproxy cluster host address '{}'", host);
      host_map_it->second.logical_host_->setNewAddresses(
          host_info->address(), host_info->addressList(), dummy_lb_endpoint_);
      return;
    }

    ENVOY_LOG(debug, "adding new dfproxy cluster host '{}'", host);

    emplaced_host = host_map_
                        .try_emplace(host, host_info,
                                     std::make_shared<Upstream::LogicalHost>(
                                         info(), std::string{host}, host_info->address(),
                                         host_info->addressList(), dummy_locality_lb_endpoint_,
                                         dummy_lb_endpoint_, nullptr, time_source_))
                        .first->second.logical_host_;
  }

  ASSERT(emplaced_host);
  if (hosts_added == nullptr) {
    hosts_added = std::make_unique<Upstream::HostVector>();
  }
  hosts_added->emplace_back(emplaced_host);
}

void Cluster::onDnsHostAddOrUpdate(
    const std::string& host,
    const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr& host_info) {
  ENVOY_LOG(debug, "Adding host info for {}", host);

  std::unique_ptr<Upstream::HostVector> hosts_added;
  addOrUpdateHost(host, host_info, hosts_added);
  if (hosts_added != nullptr) {
    ASSERT(!hosts_added->empty());
    updatePriorityState(*hosts_added, {});
  }
}

void Cluster::updatePriorityState(const Upstream::HostVector& hosts_added,
                                  const Upstream::HostVector& hosts_removed) {
  Upstream::PriorityStateManager priority_state_manager(*this, local_info_, nullptr);
  priority_state_manager.initializePriorityFor(dummy_locality_lb_endpoint_);
  {
    absl::ReaderMutexLock lock{&host_map_lock_};
    for (const auto& host : host_map_) {
      priority_state_manager.registerHostForPriority(host.second.logical_host_,
                                                     dummy_locality_lb_endpoint_);
    }
  }
  priority_state_manager.updateClusterPrioritySet(
      0, std::move(priority_state_manager.priorityState()[0].first), hosts_added, hosts_removed,
      absl::nullopt, absl::nullopt);
}

void Cluster::onDnsHostRemove(const std::string& host) {
  Upstream::HostVector hosts_removed;
  {
    absl::WriterMutexLock lock{&host_map_lock_};
    const auto host_map_it = host_map_.find(host);
    ASSERT(host_map_it != host_map_.end());
    hosts_removed.emplace_back(host_map_it->second.logical_host_);
    host_map_.erase(host);
    ENVOY_LOG(debug, "removing dfproxy cluster host '{}'", host);
  }
  updatePriorityState({}, hosts_removed);
}

Upstream::HostConstSharedPtr
Cluster::LoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  if (!context) {
    return nullptr;
  }

  const Router::StringAccessor* dynamic_host_filter_state = nullptr;
  if (context->downstreamConnection()) {
    dynamic_host_filter_state =
        context->downstreamConnection()
            ->streamInfo()
            .filterState()
            .getDataReadOnly<Router::StringAccessor>("envoy.upstream.dynamic_host");
  }

  absl::string_view host;
  if (dynamic_host_filter_state) {
    host = dynamic_host_filter_state->asString();
  } else if (context->downstreamHeaders()) {
    host = context->downstreamHeaders()->getHostValue();
  } else if (context->downstreamConnection()) {
    host = context->downstreamConnection()->requestedServerName();
  }

  if (host.empty()) {
    return nullptr;
  }

  if (cluster_.enableStrictDnsCluster()) {
    return cluster_.chooseHost(host, context);
  }

  {
    absl::ReaderMutexLock lock{&cluster_.host_map_lock_};
    const auto host_it = cluster_.host_map_.find(host);
    if (host_it == cluster_.host_map_.end()) {
      return nullptr;
    } else {
      if (host_it->second.logical_host_->coarseHealth() == Upstream::Host::Health::Unhealthy) {
        return nullptr;
      }
      host_it->second.shared_host_info_->touch();
      return host_it->second.logical_host_;
    }
  }
}

absl::optional<Upstream::SelectedPoolAndConnection>
Cluster::LoadBalancer::selectExistingConnection(Upstream::LoadBalancerContext* /*context*/,
                                                const Upstream::Host& host,
                                                std::vector<uint8_t>& hash_key) {
  const std::string& hostname = host.hostname();
  if (hostname.empty()) {
    return absl::nullopt;
  }

  LookupKey key = {hash_key, *host.address()};
  auto it = connection_info_map_.find(key);
  if (it == connection_info_map_.end()) {
    return absl::nullopt;
  }

  for (auto& info : it->second) {
    Envoy::Ssl::ConnectionInfoConstSharedPtr ssl = info.connection_->ssl();
    ASSERT(ssl);
    for (const std::string& san : ssl->dnsSansPeerCertificate()) {
      if (Extensions::TransportSockets::Tls::Utility::dnsNameMatch(hostname, san)) {
        return Upstream::SelectedPoolAndConnection{*info.pool_, *info.connection_};
      }
    }
  }

  return absl::nullopt;
}

OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks>
Cluster::LoadBalancer::lifetimeCallbacks() {
  if (!cluster_.allowCoalescedConnections()) {
    return {};
  }
  return makeOptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks>(*this);
}

void Cluster::LoadBalancer::onConnectionOpen(Envoy::Http::ConnectionPool::Instance& pool,
                                             std::vector<uint8_t>& hash_key,
                                             const Network::Connection& connection) {
  // Only coalesce connections that are over TLS.
  if (!connection.ssl()) {
    return;
  }
  const std::string alpn = connection.nextProtocol();
  if (alpn != Http::Utility::AlpnNames::get().Http2 &&
      alpn != Http::Utility::AlpnNames::get().Http3) {
    // Only coalesce connections for HTTP/2 and HTTP/3.
    return;
  }
  const LookupKey key = {hash_key, *connection.connectionInfoProvider().remoteAddress()};
  ConnectionInfo info = {&pool, &connection};
  connection_info_map_[key].push_back(info);
}

void Cluster::LoadBalancer::onConnectionDraining(Envoy::Http::ConnectionPool::Instance& pool,
                                                 std::vector<uint8_t>& hash_key,
                                                 const Network::Connection& connection) {
  const LookupKey key = {hash_key, *connection.connectionInfoProvider().remoteAddress()};
  connection_info_map_[key].erase(
      std::remove_if(connection_info_map_[key].begin(), connection_info_map_[key].end(),
                     [&pool, &connection](const ConnectionInfo& info) {
                       return (info.pool_ == &pool && info.connection_ == &connection);
                     }),
      connection_info_map_[key].end());
}

std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
ClusterFactory::createClusterWithConfig(
    Server::Configuration::ServerFactoryContext& server_context,
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context) {
  Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactoryImpl cache_manager_factory(
      context);
  envoy::config::cluster::v3::Cluster cluster_config = cluster;
  if (!cluster_config.has_upstream_http_protocol_options()) {
    // This sets defaults which will only apply if using old style http config.
    // They will be a no-op if typed_extension_protocol_options are used for
    // http config.
    cluster_config.mutable_upstream_http_protocol_options()->set_auto_sni(true);
    cluster_config.mutable_upstream_http_protocol_options()->set_auto_san_validation(true);
  }

  auto new_cluster = std::make_shared<Cluster>(server_context, cluster_config, proto_config,
                                               context, context.runtime(), cache_manager_factory,
                                               context.localInfo(), context.addedViaApi());

  // Save the cluster into cluster_info, so that we can get the cluster in the worker thread through
  // cluster_info.
  new_cluster->info()->cluster(new_cluster);

  auto& options = new_cluster->info()->upstreamHttpProtocolOptions();

  if (!proto_config.allow_insecure_cluster_options()) {
    if (!options.has_value() ||
        (!options.value().auto_sni() || !options.value().auto_san_validation())) {
      throw EnvoyException(
          "dynamic_forward_proxy cluster must have auto_sni and auto_san_validation true unless "
          "allow_insecure_cluster_options is set.");
    }
  }

  auto lb = std::make_unique<Cluster::ThreadAwareLoadBalancer>(*new_cluster);
  return std::make_pair(new_cluster, std::move(lb));
}

REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace DynamicForwardProxy
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
