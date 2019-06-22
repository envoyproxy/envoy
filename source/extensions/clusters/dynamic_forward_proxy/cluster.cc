#include "extensions/clusters/dynamic_forward_proxy/cluster.h"

#include "extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicForwardProxy {

class ClusterInfoWithOverridenTls : public Upstream::ClusterInfo {
public:
  ClusterInfoWithOverridenTls(const Upstream::ClusterInfoConstSharedPtr& real_cluster_info,
                              Network::TransportSocketFactoryPtr&& transport_socket_factory)
      : real_cluster_info_(real_cluster_info),
        transport_socket_factory_(std::move(transport_socket_factory)) {}

  // Upstream::ClusterInfo
  bool addedViaApi() const override { return real_cluster_info_->addedViaApi(); }
  std::chrono::milliseconds connectTimeout() const override {
    return real_cluster_info_->connectTimeout();
  }
  const absl::optional<std::chrono::milliseconds> idleTimeout() const override {
    return real_cluster_info_->idleTimeout();
  }
  uint32_t perConnectionBufferLimitBytes() const override {
    return real_cluster_info_->perConnectionBufferLimitBytes();
  }
  uint64_t features() const override { return real_cluster_info_->features(); }
  const Http::Http2Settings& http2Settings() const override {
    return real_cluster_info_->http2Settings();
  }
  const envoy::api::v2::Cluster::CommonLbConfig& lbConfig() const override {
    return real_cluster_info_->lbConfig();
  }
  Upstream::LoadBalancerType lbType() const override { return real_cluster_info_->lbType(); }
  envoy::api::v2::Cluster::DiscoveryType type() const override {
    return real_cluster_info_->type();
  }
  const absl::optional<envoy::api::v2::Cluster::CustomClusterType>& clusterType() const override {
    return real_cluster_info_->clusterType();
  }
  const absl::optional<envoy::api::v2::Cluster::LeastRequestLbConfig>&
  lbLeastRequestConfig() const override {
    return real_cluster_info_->lbLeastRequestConfig();
  }
  const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>&
  lbRingHashConfig() const override {
    return real_cluster_info_->lbRingHashConfig();
  }
  const absl::optional<envoy::api::v2::Cluster::OriginalDstLbConfig>&
  lbOriginalDstConfig() const override {
    return real_cluster_info_->lbOriginalDstConfig();
  }
  bool maintenanceMode() const override { return real_cluster_info_->maintenanceMode(); }
  uint64_t maxRequestsPerConnection() const override {
    return real_cluster_info_->maxRequestsPerConnection();
  }
  const std::string& name() const override { return real_cluster_info_->name(); }
  Upstream::ResourceManager& resourceManager(Upstream::ResourcePriority priority) const override {
    return real_cluster_info_->resourceManager(priority);
  }
  Network::TransportSocketFactory& transportSocketFactory() const override {
    return *transport_socket_factory_;
  }
  Upstream::ClusterStats& stats() const override { return real_cluster_info_->stats(); }
  Stats::Scope& statsScope() const override { return real_cluster_info_->statsScope(); }
  Upstream::ClusterLoadReportStats& loadReportStats() const override {
    return real_cluster_info_->loadReportStats();
  }
  const Network::Address::InstanceConstSharedPtr& sourceAddress() const override {
    return real_cluster_info_->sourceAddress();
  }
  const Upstream::LoadBalancerSubsetInfo& lbSubsetInfo() const override {
    return real_cluster_info_->lbSubsetInfo();
  }
  const envoy::api::v2::core::Metadata& metadata() const override {
    return real_cluster_info_->metadata();
  }
  const Envoy::Config::TypedMetadata& typedMetadata() const override {
    return real_cluster_info_->typedMetadata();
  }
  const Network::ConnectionSocket::OptionsSharedPtr& clusterSocketOptions() const override {
    return real_cluster_info_->clusterSocketOptions();
  }
  bool drainConnectionsOnHostRemoval() const override {
    return real_cluster_info_->drainConnectionsOnHostRemoval();
  }
  bool warmHosts() const override { return real_cluster_info_->warmHosts(); }
  absl::optional<std::string> eds_service_name() const override {
    return real_cluster_info_->eds_service_name();
  }
  Upstream::ProtocolOptionsConfigConstSharedPtr
  extensionProtocolOptions(const std::string& name) const override {
    return real_cluster_info_->extensionProtocolOptions(name);
  }

private:
  const Upstream::ClusterInfoConstSharedPtr real_cluster_info_;
  const Network::TransportSocketFactoryPtr transport_socket_factory_;
};

Cluster::Cluster(
    const envoy::api::v2::Cluster& cluster,
    const envoy::config::cluster::dynamic_forward_proxy::v2alpha::ClusterConfig& config,
    Runtime::Loader& runtime,
    Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
    const LocalInfo::LocalInfo& local_info,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : Upstream::BaseDynamicClusterImpl(cluster, runtime, factory_context, std::move(stats_scope),
                                       added_via_api),
      cluster_config_(cluster), dns_cache_manager_(cache_manager_factory.get()),
      dns_cache_(dns_cache_manager_->getCache(config.dns_cache_config())),
      update_callbacks_handle_(dns_cache_->addUpdateCallbacks(*this)), local_info_(local_info),
      transport_factory_context_(factory_context.admin(), factory_context.sslContextManager(),
                                 factory_context.statsScope(), factory_context.clusterManager(),
                                 factory_context.localInfo(), factory_context.dispatcher(),
                                 factory_context.random(), factory_context.stats(),
                                 factory_context.singletonManager(), factory_context.threadLocal(),
                                 factory_context.messageValidationVisitor(), factory_context.api()),
      host_map_(std::make_shared<HostInfoMap>()) {
  // TODO(mattklein123): Technically, we should support attaching to an already warmed DNS cache.
  //                     This will require adding a hosts() or similar API to the cache and
  //                     reading it during initialization.

  // Block certain TLS context parameters that don't make sense on a cluster-wide scale. We will
  // support these parameters dynamically in the future. This is not an exhaustive list of
  // parameters that don't make sense but should be the most obvious ones that a user might set
  // in error.
  if (!cluster.tls_context().sni().empty() || !cluster.tls_context()
                                                   .common_tls_context()
                                                   .validation_context()
                                                   .verify_subject_alt_name()
                                                   .empty()) {
    throw EnvoyException(
        "dynamic_forward_proxy cluster cannot configure 'sni' or 'verify_subject_alt_name'");
  }
}

void Cluster::onDnsHostAddOrUpdate(
    const std::string& host,
    const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr& host_info) {
  // We should never get a host with no address from the cache.
  ASSERT(host_info->address() != nullptr);

  // NOTE: Right now we allow a DNS cache to be shared between multiple clusters. Though we have
  // connection/request circuit breakers on the cluster, we don't have any way to control the
  // maximum hosts on a cluster. We currently assume that host data shared via shared pointer is a
  // marginal memory cost above that already used by connections and requests, so relying on
  // connection/request circuit breakers is sufficient. We may have to revisit this in the future.

  HostInfoMapSharedPtr current_map = getCurrentHostMap();
  const auto host_map_it = current_map->find(host);
  if (host_map_it != current_map->end()) {
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
  Upstream::ClusterInfoConstSharedPtr cluster_info_to_use;
  if (createCustomTlsForHost()) {
    // Create an override cluster configuration that automatically provides both SNI as well as
    // SAN verification for the resolved host if the cluster has been configured with TLS.
    // TODO(mattklein123): The fact that we are copying the cluster config, etc. is not very clean.
    //                     consider streamlining this in the future.
    // TODO(mattklein123): If the host is an IP address should we be setting SNI? IP addresses in
    //                     hosts needs to be revisited so this can be handled in a follow up.
    envoy::api::v2::Cluster override_cluster = cluster_config_;
    override_cluster.mutable_tls_context()->set_sni(host_info->resolvedHost());
    override_cluster.mutable_tls_context()
        ->mutable_common_tls_context()
        ->mutable_validation_context()
        ->add_verify_subject_alt_name(host_info->resolvedHost());
    cluster_info_to_use = std::make_shared<ClusterInfoWithOverridenTls>(
        info(),
        Upstream::createTransportSocketFactory(override_cluster, transport_factory_context_));
  } else {
    cluster_info_to_use = info();
  }

  const auto new_host_map = std::make_shared<HostInfoMap>(*current_map);
  const auto emplaced = new_host_map->try_emplace(
      host, host_info,
      std::make_shared<Upstream::LogicalHost>(cluster_info_to_use, host, host_info->address(),
                                              dummy_locality_lb_endpoint_, dummy_lb_endpoint_));
  Upstream::HostVector hosts_added;
  hosts_added.emplace_back(emplaced.first->second.logical_host_);

  // Swap in the new map. This will be picked up when the per-worker LBs are recreated via
  // the host set update.
  swapAndUpdateMap(new_host_map, hosts_added, {});
}

bool Cluster::createCustomTlsForHost() {
  // TODO(mattklein123): Consider custom settings per host and/or global cluster config to turn this
  // off.
  return !cluster_config_.has_transport_socket() && cluster_config_.has_tls_context();
}

void Cluster::swapAndUpdateMap(const HostInfoMapSharedPtr& new_hosts_map,
                               const Upstream::HostVector& hosts_added,
                               const Upstream::HostVector& hosts_removed) {
  {
    absl::WriterMutexLock lock(&host_map_lock_);
    host_map_ = new_hosts_map;
  }

  Upstream::PriorityStateManager priority_state_manager(*this, local_info_, nullptr);
  priority_state_manager.initializePriorityFor(dummy_locality_lb_endpoint_);
  for (const auto& host : (*new_hosts_map)) {
    priority_state_manager.registerHostForPriority(host.second.logical_host_,
                                                   dummy_locality_lb_endpoint_);
  }
  priority_state_manager.updateClusterPrioritySet(
      0, std::move(priority_state_manager.priorityState()[0].first), hosts_added, hosts_removed,
      absl::nullopt, absl::nullopt);
}

void Cluster::onDnsHostRemove(const std::string& host) {
  HostInfoMapSharedPtr current_map = getCurrentHostMap();
  const auto host_map_it = current_map->find(host);
  ASSERT(host_map_it != current_map->end());
  const auto new_host_map = std::make_shared<HostInfoMap>(*current_map);
  Upstream::HostVector hosts_removed;
  hosts_removed.emplace_back(host_map_it->second.logical_host_);
  new_host_map->erase(host);
  ENVOY_LOG(debug, "removing dfproxy cluster host '{}'", host);

  // Swap in the new map. This will be picked up when the per-worker LBs are recreated via
  // the host set update.
  swapAndUpdateMap(new_host_map, {}, hosts_removed);
}

Upstream::HostConstSharedPtr
Cluster::LoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  if (!context || !context->downstreamHeaders()) {
    return nullptr;
  }

  const auto host_it =
      host_map_->find(context->downstreamHeaders()->Host()->value().getStringView());
  if (host_it == host_map_->end()) {
    return nullptr;
  } else {
    host_it->second.shared_host_info_->touch();
    return host_it->second.logical_host_;
  }
}

std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
ClusterFactory::createClusterWithConfig(
    const envoy::api::v2::Cluster& cluster,
    const envoy::config::cluster::dynamic_forward_proxy::v2alpha::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
    Stats::ScopePtr&& stats_scope) {
  Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactoryImpl cache_manager_factory(
      context.singletonManager(), context.dispatcher(), context.tls(), context.stats());
  auto new_cluster = std::make_shared<Cluster>(
      cluster, proto_config, context.runtime(), cache_manager_factory, context.localInfo(),
      socket_factory_context, std::move(stats_scope), context.addedViaApi());
  auto lb = std::make_unique<Cluster::ThreadAwareLoadBalancer>(*new_cluster);
  return std::make_pair(new_cluster, std::move(lb));
}

REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace DynamicForwardProxy
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
