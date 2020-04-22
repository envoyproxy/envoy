#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/empty_string.h"
#include "common/common/logger.h"
#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/clusters/well_known_names.h"

namespace Envoy {
namespace Upstream {

using HostMapSharedPtr = std::shared_ptr<HostMap>;
using HostMapConstSharedPtr = std::shared_ptr<const HostMap>;

/**
 * The OriginalDstCluster is a dynamic cluster that automatically adds hosts as needed based on the
 * original destination address of the downstream connection. These hosts are also automatically
 * cleaned up after they have not seen traffic for a configurable cleanup interval time
 * ("cleanup_interval_ms").
 */
class OriginalDstCluster : public ClusterImplBase {
public:
  OriginalDstCluster(const envoy::config::cluster::v3::Cluster& config, Runtime::Loader& runtime,
                     Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
                     Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

  /**
   * Special Load Balancer for Original Dst Cluster.
   *
   * Load balancer gets called with the downstream context which can be used to make sure the
   * Original Dst cluster has a Host for the original destination. Normally load balancers can't
   * modify clusters, but in this case we access a singleton OriginalDstCluster that we can ask to
   * add hosts on demand. Additions are synced with all other threads so that the host set in the
   * cluster remains (eventually) consistent. If multiple threads add a host to the same upstream
   * address then two distinct HostSharedPtr's (with the same upstream IP address) will be added,
   * and both of them will eventually time out.
   */
  class LoadBalancer : public Upstream::LoadBalancer {
  public:
    LoadBalancer(const std::shared_ptr<OriginalDstCluster>& parent)
        : parent_(parent), host_map_(parent->getCurrentHostMap()) {}

    // Upstream::LoadBalancer
    HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

  private:
    Network::Address::InstanceConstSharedPtr requestOverrideHost(LoadBalancerContext* context);

    const std::shared_ptr<OriginalDstCluster> parent_;
    HostMapConstSharedPtr host_map_;
  };

private:
  struct LoadBalancerFactory : public Upstream::LoadBalancerFactory {
    LoadBalancerFactory(const std::shared_ptr<OriginalDstCluster>& cluster) : cluster_(cluster) {}

    // Upstream::LoadBalancerFactory
    Upstream::LoadBalancerPtr create() override { return std::make_unique<LoadBalancer>(cluster_); }

    const std::shared_ptr<OriginalDstCluster> cluster_;
  };

  struct ThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
    ThreadAwareLoadBalancer(const std::shared_ptr<OriginalDstCluster>& cluster)
        : cluster_(cluster) {}

    // Upstream::ThreadAwareLoadBalancer
    Upstream::LoadBalancerFactorySharedPtr factory() override {
      return std::make_shared<LoadBalancerFactory>(cluster_);
    }
    void initialize() override {}

    const std::shared_ptr<OriginalDstCluster> cluster_;
  };

  HostMapConstSharedPtr getCurrentHostMap() {
    absl::ReaderMutexLock lock(&host_map_lock_);
    return host_map_;
  }

  void setHostMap(const HostMapConstSharedPtr& new_host_map) {
    absl::WriterMutexLock lock(&host_map_lock_);
    host_map_ = new_host_map;
  }

  void addHost(HostSharedPtr&);
  void cleanup();

  // ClusterImplBase
  void startPreInit() override { onPreInitComplete(); }

  Event::Dispatcher& dispatcher_;
  const std::chrono::milliseconds cleanup_interval_ms_;
  Event::TimerPtr cleanup_timer_;
  const bool use_http_header_;

  absl::Mutex host_map_lock_;
  HostMapConstSharedPtr host_map_ ABSL_GUARDED_BY(host_map_lock_);

  friend class OriginalDstClusterFactory;
};

using OriginalDstClusterSharedPtr = std::shared_ptr<OriginalDstCluster>;

class OriginalDstClusterFactory : public ClusterFactoryImplBase {
public:
  OriginalDstClusterFactory()
      : ClusterFactoryImplBase(Extensions::Clusters::ClusterTypes::get().OriginalDst) {}

private:
  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr> createClusterImpl(
      const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopePtr&& stats_scope) override;
};

} // namespace Upstream
} // namespace Envoy
