#include "source/extensions/clusters/dynamic_forward_proxy/cluster.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.validate.h"

#include "source/common/network/transport_socket_options_impl.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicForwardProxy {

Cluster::Cluster(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig& config,
    Runtime::Loader& runtime,
    Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
    const LocalInfo::LocalInfo& local_info,
    Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : Upstream::BaseDynamicClusterImpl(cluster, runtime, factory_context, std::move(stats_scope),
                                       added_via_api, factory_context.dispatcher().timeSource()),
      dns_cache_manager_(cache_manager_factory.get()),
      dns_cache_(dns_cache_manager_->getCache(config.dns_cache_config())),
      update_callbacks_handle_(dns_cache_->addUpdateCallbacks(*this)), local_info_(local_info) {}

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
    // We should never get a host with no address from the cache.
    ASSERT(host_info->address() != nullptr);

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
      host_map_it->second.logical_host_->setNewAddress(host_info->address(), dummy_lb_endpoint_);
      return;
    }

    ENVOY_LOG(debug, "adding new dfproxy cluster host '{}'", host);

    emplaced_host = host_map_
                        .try_emplace(host, host_info,
                                     std::make_shared<Upstream::LogicalHost>(
                                         info(), std::string{host}, host_info->address(),
                                         dummy_locality_lb_endpoint_, dummy_lb_endpoint_, nullptr,
                                         time_source_))
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
      host_it->second.shared_host_info_->touch();
      return host_it->second.logical_host_;
    }
  }
}

std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
ClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
    Stats::ScopePtr&& stats_scope) {
  Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactoryImpl cache_manager_factory(
      context.singletonManager(), context.dispatcher(), context.tls(),
      context.api().randomGenerator(), context.runtime(), context.stats());
  envoy::config::cluster::v3::Cluster cluster_config = cluster;
  if (cluster_config.has_upstream_http_protocol_options()) {
    if (!proto_config.allow_insecure_cluster_options() &&
        (!cluster_config.upstream_http_protocol_options().auto_sni() ||
         !cluster_config.upstream_http_protocol_options().auto_san_validation())) {
      throw EnvoyException(
          "dynamic_forward_proxy cluster must have auto_sni and auto_san_validation true when "
          "configured with upstream_http_protocol_options");
    }
  } else {
    cluster_config.mutable_upstream_http_protocol_options()->set_auto_sni(true);
    cluster_config.mutable_upstream_http_protocol_options()->set_auto_san_validation(true);
  }

  auto new_cluster = std::make_shared<Cluster>(
      cluster_config, proto_config, context.runtime(), cache_manager_factory, context.localInfo(),
      socket_factory_context, std::move(stats_scope), context.addedViaApi());
  auto lb = std::make_unique<Cluster::ThreadAwareLoadBalancer>(*new_cluster);
  return std::make_pair(new_cluster, std::move(lb));
}

REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace DynamicForwardProxy
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
