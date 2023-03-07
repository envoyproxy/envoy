#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.validate.h"
#include "envoy/http/conn_pool.h"

#include "source/common/upstream/cluster_factory_impl.h"
#include "source/extensions/clusters/common/logical_host.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicForwardProxy {

class Cluster : public Upstream::BaseDynamicClusterImpl,
                public Extensions::Common::DynamicForwardProxy::DnsCache::UpdateCallbacks {
public:
  Cluster(Server::Configuration::ServerFactoryContext& server_context,
          const envoy::config::cluster::v3::Cluster& cluster,
          const envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig& config,
          Runtime::Loader& runtime,
          Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
          const LocalInfo::LocalInfo& local_info,
          Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
          Stats::ScopeSharedPtr&& stats_scope, bool added_via_api);

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

private:
  struct HostInfo {
    HostInfo(const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr& shared_host_info,
             const Upstream::LogicalHostSharedPtr& logical_host)
        : shared_host_info_(shared_host_info), logical_host_(logical_host) {}

    const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr shared_host_info_;
    const Upstream::LogicalHostSharedPtr logical_host_;
  };

  using HostInfoMap = absl::flat_hash_map<std::string, HostInfo>;

  class LoadBalancer : public Upstream::LoadBalancer,
                       public Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks {
  public:
    LoadBalancer(const Cluster& cluster) : cluster_(cluster) {}

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
    Upstream::LoadBalancerPtr create() override { return std::make_unique<LoadBalancer>(cluster_); }
    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override { return create(); }

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
    void initialize() override {}

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

  // True if H2 and H3 connections may be reused across different origins.
  const bool allow_coalesced_connections_;

  mutable absl::Mutex host_map_lock_;
  HostInfoMap host_map_ ABSL_GUARDED_BY(host_map_lock_);

  friend class ClusterFactory;
  friend class ClusterTest;
};

class ClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                           envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig> {
public:
  ClusterFactory() : ConfigurableClusterFactoryBase("envoy.clusters.dynamic_forward_proxy") {}

private:
  std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
  createClusterWithConfig(
      Server::Configuration::ServerFactoryContext& server_context,
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopeSharedPtr&& stats_scope) override;
};

DECLARE_FACTORY(ClusterFactory);

} // namespace DynamicForwardProxy
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
