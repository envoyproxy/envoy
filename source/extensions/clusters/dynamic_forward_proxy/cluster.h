#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.validate.h"

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
  Cluster(const envoy::config::cluster::v3::Cluster& cluster,
          const envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig& config,
          Runtime::Loader& runtime,
          Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
          const LocalInfo::LocalInfo& local_info,
          Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
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

  struct LoadBalancer : public Upstream::LoadBalancer {
    LoadBalancer(const Cluster& cluster) : cluster_(cluster) {}

    // Upstream::LoadBalancer
    Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;
    // Preconnecting not implemented.
    Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
      return nullptr;
    }

    const Cluster& cluster_;
  };

  struct LoadBalancerFactory : public Upstream::LoadBalancerFactory {
    LoadBalancerFactory(Cluster& cluster) : cluster_(cluster) {}

    // Upstream::LoadBalancerFactory
    Upstream::LoadBalancerPtr create() override { return std::make_unique<LoadBalancer>(cluster_); }

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

  void
  addOrUpdateHost(absl::string_view host,
                  const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr& host_info,
                  std::unique_ptr<Upstream::HostVector>& hosts_added)
      ABSL_LOCKS_EXCLUDED(host_map_lock_);

  void updatePriorityState(const Upstream::HostVector& hosts_added,
                           const Upstream::HostVector& hosts_removed)
      ABSL_LOCKS_EXCLUDED(host_map_lock_);

  const Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr dns_cache_manager_;
  const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dns_cache_;
  const Extensions::Common::DynamicForwardProxy::DnsCache::AddUpdateCallbacksHandlePtr
      update_callbacks_handle_;
  const envoy::config::endpoint::v3::LocalityLbEndpoints dummy_locality_lb_endpoint_;
  const envoy::config::endpoint::v3::LbEndpoint dummy_lb_endpoint_;
  const LocalInfo::LocalInfo& local_info_;

  mutable absl::Mutex host_map_lock_;
  HostInfoMap host_map_ ABSL_GUARDED_BY(host_map_lock_);

  friend class ClusterFactory;
  friend class ClusterTest;

  TimeSource& time_source_;
};

class ClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                           envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig> {
public:
  ClusterFactory()
      : ConfigurableClusterFactoryBase(
            Extensions::Clusters::ClusterTypes::get().DynamicForwardProxy) {}

private:
  std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
  createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopePtr&& stats_scope) override;
};

DECLARE_FACTORY(ClusterFactory);

} // namespace DynamicForwardProxy
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
