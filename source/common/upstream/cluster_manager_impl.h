#pragma once

#include "sds.h"

#include "envoy/http/codes.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/async_client_impl.h"
#include "common/json/json_loader.h"

namespace Upstream {

/**
 * Implementation of ClusterManager that reads from a JSON configuration, maintains a central
 * cluster list, as well as thread local caches of each cluster and associated connection pools.
 */
class ClusterManagerImpl : public ClusterManager {
public:
  ClusterManagerImpl(const Json::Object& config, Stats::Store& stats, ThreadLocal::Instance& tls,
                     Network::DnsResolver& dns_resolver, Ssl::ContextManager& ssl_context_manager,
                     Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                     const std::string& local_zone_name, const std::string& local_address);

  // Upstream::ClusterManager
  void setInitializedCb(std::function<void()> callback) override {
    if (pending_cluster_init_ == 0) {
      callback();
    } else {
      initialized_callback_ = callback;
    }
  }

  std::unordered_map<std::string, ConstClusterPtr> clusters() override {
    std::unordered_map<std::string, ConstClusterPtr> clusters_map;
    for (auto& cluster : primary_clusters_) {
      clusters_map[cluster.first] = cluster.second;
    }

    return clusters_map;
  }

  const Cluster* get(const std::string& cluster) override;
  Http::ConnectionPool::Instance* httpConnPoolForCluster(const std::string& cluster,
                                                         ResourcePriority priority) override;
  Host::CreateConnectionData tcpConnForCluster(const std::string& cluster) override;
  Http::AsyncClient& httpAsyncClientForCluster(const std::string& cluster) override;

  void shutdown() override {
    for (auto& cluster : primary_clusters_) {
      cluster.second->shutdown();
    }
  }

protected:
  Runtime::Loader& runtime_;

private:
  /**
   * Thread local cached cluster data. Each thread local cluster gets updates from the parent
   * central dynamic cluster (if applicable). It maintains load balancer state and any created
   * connection pools.
   */
  struct ThreadLocalClusterManagerImpl : public ThreadLocal::ThreadLocalObject {
    struct ConnPoolsContainer {
      typedef std::array<Http::ConnectionPool::InstancePtr, NumResourcePriorities> ConnPools;

      ConnPools pools_;
      uint64_t drains_remaining_{};
    };

    struct ClusterEntry {
      ClusterEntry(ThreadLocalClusterManagerImpl& parent, const Cluster& cluster,
                   Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                   Stats::Store& stats_store, Event::Dispatcher& dispatcher,
                   const std::string& local_zone_name, const std::string& local_address,
                   const Optional<std::string>& local_cluster_name);

      Http::ConnectionPool::Instance* connPool(ResourcePriority priority);

      ThreadLocalClusterManagerImpl& parent_;
      HostSetImpl host_set_;
      LoadBalancerPtr lb_;
      const Cluster& primary_cluster_;
      Http::AsyncClientImpl http_async_client_;
    };

    typedef std::unique_ptr<ClusterEntry> ClusterEntryPtr;

    ThreadLocalClusterManagerImpl(ClusterManagerImpl& parent, Event::Dispatcher& dispatcher,
                                  Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                                  const std::string& local_zone_name,
                                  const std::string& local_address,
                                  const Optional<std::string>& local_cluster_name);
    void drainConnPools(HostPtr old_host, ConnPoolsContainer& container);
    static void updateClusterMembership(const std::string& name, ConstHostVectorPtr hosts,
                                        ConstHostVectorPtr healthy_hosts,
                                        ConstHostVectorPtr local_zone_hosts,
                                        ConstHostVectorPtr local_zone_healthy_hosts,
                                        const std::vector<HostPtr>& hosts_added,
                                        const std::vector<HostPtr>& hosts_removed,
                                        ThreadLocal::Instance& tls, uint32_t thread_local_slot);

    // ThreadLocal::ThreadLocalObject
    void shutdown() override;

    ClusterManagerImpl& parent_;
    Event::Dispatcher& dispatcher_;
    std::unordered_map<std::string, ClusterEntryPtr> thread_local_clusters_;
    std::unordered_map<ConstHostPtr, ConnPoolsContainer> host_http_conn_pool_map_;
  };

  virtual Http::ConnectionPool::InstancePtr allocateConnPool(Event::Dispatcher& dispatcher,
                                                             ConstHostPtr host, Stats::Store& store,
                                                             ResourcePriority priority) PURE;
  void loadCluster(const Json::Object& cluster, Stats::Store& stats,
                   Network::DnsResolver& dns_resolver, Ssl::ContextManager& ssl_context_manager,
                   Runtime::Loader& runtime, Runtime::RandomGenerator& random);
  void postThreadLocalClusterUpdate(const ClusterImplBase& primary_cluster,
                                    const std::vector<HostPtr>& hosts_added,
                                    const std::vector<HostPtr>& hosts_removed);

  ThreadLocal::Instance& tls_;
  Stats::Store& stats_;
  uint32_t thread_local_slot_;
  std::unordered_map<std::string, ClusterImplBasePtr> primary_clusters_;
  std::function<void()> initialized_callback_;
  uint32_t pending_cluster_init_;
  Optional<SdsConfig> sds_config_;
  std::list<SdsClusterImpl*> sds_clusters_;
};

/**
 * Prod implementation of ClusterManagerImpl that allocates real connection pools.
 */
class ProdClusterManagerImpl : public ClusterManagerImpl {
public:
  using ClusterManagerImpl::ClusterManagerImpl;

private:
  // ClusterManagerImpl
  Http::ConnectionPool::InstancePtr allocateConnPool(Event::Dispatcher& dispatcher,
                                                     ConstHostPtr host, Stats::Store& store,
                                                     ResourcePriority priority) override;
};

} // Upstream
