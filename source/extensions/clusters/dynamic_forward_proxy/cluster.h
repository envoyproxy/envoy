#pragma once

#include "envoy/config/cluster/dynamic_forward_proxy/v2alpha/cluster.pb.h"
#include "envoy/config/cluster/dynamic_forward_proxy/v2alpha/cluster.pb.validate.h"

#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/logical_host.h"

#include "extensions/clusters/well_known_names.h"
#include "extensions/common/dynamic_forward_proxy/dns_cache.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicForwardProxy {

class Cluster : public Upstream::BaseDynamicClusterImpl,
                public Extensions::Common::DynamicForwardProxy::DnsCache::UpdateCallbacks {
public:
  Cluster(const envoy::api::v2::Cluster& cluster,
          const envoy::config::cluster::dynamic_forward_proxy::v2alpha::ClusterConfig& config,
          Runtime::Loader& runtime,
          Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
          const LocalInfo::LocalInfo& local_info,
          Server::Configuration::TransportSocketFactoryContext& factory_context,
          Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  Upstream::Cluster::InitializePhase initializePhase() const override {
    return Upstream::Cluster::InitializePhase::Primary;
  }

  // Upstream::ClusterImplBase
  void startPreInit() override;

  // Extensions::Common::DynamicForwardProxy::DnsCache::UpdateCallbacks
  void onDnsHostAddOrUpdate(
      const std::string& host,
      const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr& host_info) override;
  void onDnsHostRemove(const std::string& host) override;

private:
  struct HostInfo {
    HostInfo(const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr& shared_host_info,
             const Upstream::LogicalHostSharedPtr& logical_host)
        : shared_host_info_(shared_host_info), logical_host_(logical_host) {}

    const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr shared_host_info_;
    const Upstream::LogicalHostSharedPtr logical_host_;
  };

  using HostInfoMap = absl::flat_hash_map<std::string, HostInfo>;
  using HostInfoMapSharedPtr = std::shared_ptr<const HostInfoMap>;

  struct LoadBalancer : public Upstream::LoadBalancer {
    LoadBalancer(const HostInfoMapSharedPtr& host_map) : host_map_(host_map) {}

    // Upstream::LoadBalancer
    Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;

    const HostInfoMapSharedPtr host_map_;
  };

  struct LoadBalancerFactory : public Upstream::LoadBalancerFactory {
    LoadBalancerFactory(Cluster& cluster) : cluster_(cluster) {}

    // Upstream::LoadBalancerFactory
    Upstream::LoadBalancerPtr create() override {
      return std::make_unique<LoadBalancer>(cluster_.getCurrentHostMap());
    }

    Cluster& cluster_;
  };

  struct ThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
    ThreadAwareLoadBalancer(Cluster& cluster) : cluster_(cluster) {}

    // Upstream::ThreadAwareLoadBalancer
    Upstream::LoadBalancerFactorySharedPtr factory() override {
      return std::make_shared<LoadBalancerFactory>(cluster_);
    }
    void initialize() override {}

    Cluster& cluster_;
  };

  HostInfoMapSharedPtr getCurrentHostMap() {
    absl::ReaderMutexLock lock(&host_map_lock_);
    return host_map_;
  }

  void
  addOrUpdateWorker(const std::string& host,
                    const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr& host_info,
                    std::shared_ptr<HostInfoMap>& new_host_map,
                    std::unique_ptr<Upstream::HostVector>& hosts_added);
  void swapAndUpdateMap(const HostInfoMapSharedPtr& new_hosts_map,
                        const Upstream::HostVector& hosts_added,
                        const Upstream::HostVector& hosts_removed);

  const Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr dns_cache_manager_;
  const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dns_cache_;
  const Extensions::Common::DynamicForwardProxy::DnsCache::AddUpdateCallbacksHandlePtr
      update_callbacks_handle_;
  const envoy::api::v2::endpoint::LocalityLbEndpoints dummy_locality_lb_endpoint_;
  const envoy::api::v2::endpoint::LbEndpoint dummy_lb_endpoint_;
  const LocalInfo::LocalInfo& local_info_;

  absl::Mutex host_map_lock_;
  HostInfoMapSharedPtr host_map_ ABSL_GUARDED_BY(host_map_lock_);

  friend class ClusterFactory;
  friend class ClusterTest;
};

class ClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                           envoy::config::cluster::dynamic_forward_proxy::v2alpha::ClusterConfig> {
public:
  ClusterFactory()
      : ConfigurableClusterFactoryBase(
            Extensions::Clusters::ClusterTypes::get().DynamicForwardProxy) {}

private:
  std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
  createClusterWithConfig(
      const envoy::api::v2::Cluster& cluster,
      const envoy::config::cluster::dynamic_forward_proxy::v2alpha::ClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
      Stats::ScopePtr&& stats_scope) override;
};

DECLARE_FACTORY(ClusterFactory);

} // namespace DynamicForwardProxy
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
