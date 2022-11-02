#include "source/extensions/clusters/dynamic_forward_proxy/cluster.h"

#include <algorithm>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.validate.h"

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
    Runtime::Loader& runtime,
    Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
    const LocalInfo::LocalInfo& local_info,
    Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
    Stats::ScopeSharedPtr&& stats_scope, bool added_via_api)
    : Upstream::BaseDynamicClusterImpl(server_context, cluster, runtime, factory_context,
                                       std::move(stats_scope), added_via_api,
                                       factory_context.mainThreadDispatcher().timeSource()),
      dns_cache_manager_(cache_manager_factory.get()),
      dns_cache_(dns_cache_manager_->getCache(config.dns_cache_config())),
      update_callbacks_handle_(dns_cache_->addUpdateCallbacks(*this)), local_info_(local_info),
      allow_coalesced_connections_(config.allow_coalesced_connections()) {}

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

void Cluster::addOrUpdateHost(
    absl::string_view host,
    const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr& host_info,
    std::unique_ptr<Upstream::HostVector>& hosts_added) {
  Upstream::LogicalHostSharedPtr emplaced_host;
  {
    absl::WriterMutexLock lock{&host_map_lock_};

    // NOTE: Right now we allow a DNS cache to be shared between multiple clusters. Though we have
    // connection/request circuit breakers on the cluster, we don't have any way to control the
    // maximum hosts on a cluster. We currently assume that host data shared via shared pointer is a
    // marginal memory cost above that already used by connections and requests, so relying on
    // connection/request circuit breakers is sufficient. We may have to revisit this in the future.
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

  absl::string_view host;
  if (context->downstreamHeaders()) {
    host = context->downstreamHeaders()->getHostValue();
  } else if (context->downstreamConnection()) {
    host = context->downstreamConnection()->requestedServerName();
  }

  if (host.empty()) {
    return nullptr;
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
    Upstream::ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
    Stats::ScopeSharedPtr&& stats_scope) {
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

  auto new_cluster = std::make_shared<Cluster>(
      server_context, cluster_config, proto_config, context.runtime(), cache_manager_factory,
      context.localInfo(), socket_factory_context, std::move(stats_scope), context.addedViaApi());

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
