#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.validate.h"
#include "envoy/http/conn_pool.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/extensions/clusters/common/logical_host.h"
#include "source/extensions/common/dynamic_forward_proxy/cluster_store.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicForwardProxy {

class ClusterFactory;
class ClusterTest;

class Cluster : public Upstream::BaseDynamicClusterImpl,
                public Extensions::Common::DynamicForwardProxy::DfpCluster,
                public Extensions::Common::DynamicForwardProxy::DnsCache::UpdateCallbacks {
public:
  ~Cluster() override;

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
  void onDnsResolutionComplete(const std::string&,
                               const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr&,
                               Network::DnsResolver::ResolutionStatus) override {}

  bool allowCoalescedConnections() const { return allow_coalesced_connections_; }
  bool enableSubCluster() const override { return enable_sub_cluster_; }
  Upstream::HostConstSharedPtr chooseHost(absl::string_view host,
                                          Upstream::LoadBalancerContext* context) const;

  // Extensions::Common::DynamicForwardProxy::DfpCluster
  std::pair<bool, absl::optional<envoy::config::cluster::v3::Cluster>>
  createSubClusterConfig(const std::string& cluster_name, const std::string& host,
                         const int port) override;
  bool touch(const std::string& cluster_name) override;
  void checkIdleSubCluster();

protected:
  Cluster(const envoy::config::cluster::v3::Cluster& cluster,
          Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr&& cacahe,
          const envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig& config,
          Upstream::ClusterFactoryContext& context,
          Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr&& cache_manager,
          absl::Status& creation_status);

private:
  friend class ClusterFactory;
  friend class ClusterTest;

  struct ClusterInfo {
    ClusterInfo(std::string cluster_name, Cluster& parent);
    void touch();
    bool checkIdle();

    std::string cluster_name_;
    Cluster& parent_;
    std::atomic<std::chrono::steady_clock::duration> last_used_time_;
  };

  using ClusterInfoMap = absl::flat_hash_map<std::string, std::shared_ptr<ClusterInfo>>;

  struct HostInfo {
    HostInfo(const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr& shared_host_info,
             const Upstream::LogicalHostSharedPtr& logical_host)
        : shared_host_info_(shared_host_info), logical_host_(logical_host) {}

    const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr shared_host_info_;
    const Upstream::LogicalHostSharedPtr logical_host_;
  };

  using HostInfoMap = absl::flat_hash_map<std::string, HostInfo>;

  class LoadBalancer : public Upstream::LoadBalancer,
                       public Extensions::Common::DynamicForwardProxy::DfpLb,
                       public Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks {
  public:
    LoadBalancer(const Cluster& cluster) : cluster_(cluster) {}

    // DfpLb
    Upstream::HostConstSharedPtr findHostByName(const std::string& host) const override;
    // Upstream::LoadBalancer
    Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext* context) override;
    // Preconnecting not implemented.
    Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
      return nullptr;
    }
    absl::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(Upstream::LoadBalancerContext* context, const Upstream::Host& host,
                             std::vector<uint8_t>& hash_key) override;
    OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override;

    // Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks
    void onConnectionOpen(Envoy::Http::ConnectionPool::Instance& pool,
                          std::vector<uint8_t>& hash_key,
                          const Network::Connection& connection) override;

    void onConnectionDraining(Envoy::Http::ConnectionPool::Instance& pool,
                              std::vector<uint8_t>& hash_key,
                              const Network::Connection& connection) override;

  private:
    struct ConnectionInfo {
      Envoy::Http::ConnectionPool::Instance* pool_; // Not a ref to allow assignment in remove().
      const Network::Connection* connection_;       // Not a ref to allow assignment in remove().
    };
    struct LookupKey {
      const std::vector<uint8_t> hash_key_;
      const Network::Address::Instance& peer_address_;
      bool operator==(const LookupKey& rhs) const {
        return std::tie(hash_key_, peer_address_) == std::tie(rhs.hash_key_, rhs.peer_address_);
      }
    };
    struct LookupKeyHash {
      size_t operator()(const LookupKey& lookup_key) const {
        return std::hash<std::string>{}(lookup_key.peer_address_.asString());
      }
    };

    absl::flat_hash_map<LookupKey, std::vector<ConnectionInfo>, LookupKeyHash> connection_info_map_;

    const Cluster& cluster_;
  };

  class LoadBalancerFactory : public Upstream::LoadBalancerFactory {
  public:
    LoadBalancerFactory(Cluster& cluster) : cluster_(cluster) {}

    // Upstream::LoadBalancerFactory
    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override {
      return std::make_unique<LoadBalancer>(cluster_);
    }

  private:
    Cluster& cluster_;
  };

  class ThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
  public:
    ThreadAwareLoadBalancer(Cluster& cluster) : cluster_(cluster) {}

    // Upstream::ThreadAwareLoadBalancer
    Upstream::LoadBalancerFactorySharedPtr factory() override {
      return std::make_shared<LoadBalancerFactory>(cluster_);
    }
    absl::Status initialize() override { return absl::OkStatus(); }

  private:
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
  Event::Dispatcher& main_thread_dispatcher_;
  const envoy::config::cluster::v3::Cluster orig_cluster_config_;

  Event::TimerPtr idle_timer_;

  // True if H2 and H3 connections may be reused across different origins.
  const bool allow_coalesced_connections_;

  mutable absl::Mutex host_map_lock_;
  HostInfoMap host_map_ ABSL_GUARDED_BY(host_map_lock_);

  mutable absl::Mutex cluster_map_lock_;
  ClusterInfoMap cluster_map_ ABSL_GUARDED_BY(cluster_map_lock_);

  Upstream::ClusterManager& cm_;
  const size_t max_sub_clusters_;
  const std::chrono::milliseconds sub_cluster_ttl_;
  const envoy::config::cluster::v3::Cluster_LbPolicy sub_cluster_lb_policy_;
  const bool enable_sub_cluster_;

  friend class ClusterFactory;
  friend class ClusterTest;
};

class ClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                           envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig> {
public:
  ClusterFactory() : ConfigurableClusterFactoryBase("envoy.clusters.dynamic_forward_proxy") {}

private:
  absl::StatusOr<
      std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context) override;
};

DECLARE_FACTORY(ClusterFactory);

} // namespace DynamicForwardProxy
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
