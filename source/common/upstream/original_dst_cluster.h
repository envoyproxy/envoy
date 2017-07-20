#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "envoy/thread_local/thread_local.h"

#include "common/common/empty_string.h"
#include "common/common/logger.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * The OriginalDstCluster is a dynamic cluster that automatically adds hosts as needed based on the
 * original destination address of the downstream connection.  These hosts are also automatically
 * cleaned up after they have not seen traffic for a configurable cleanup interval time
 * ("cleanup_interval_ms").
 */
class OriginalDstCluster : public ClusterImplBase {
public:
  OriginalDstCluster(const Json::Object& config, Runtime::Loader& runtime, Stats::Store& stats,
                     Ssl::ContextManager& ssl_context_manager, Event::Dispatcher& dispatcher,
                     bool added_via_api);
  ~OriginalDstCluster() {}

  // Upstream::Cluster
  void initialize() override {}
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }
  void setInitializedCb(std::function<void()> callback) override { callback(); }

  /**
   * Special Load Balancer for Original Dst Cluster.
   *
   * Load balancer gets called with the downstream context which can be used to make sure the
   * Original Dst cluster has a Host for the original destination.  Normally load balancers can't
   * modify clusters, but in this case we access a singleton OriginalDstCluster that we can ask to
   * add hosts on demand.  Additions are synced with all other threads so that the host set in the
   * cluster remains (eventually) consistent.  If multiple threads add a host to the same upstream
   * address then two distinct HostSharedPtr's (with the same upstream IP address) will be added,
   * and both of them will eventually time out.
   */
  class LoadBalancer : public Upstream::LoadBalancer {
  public:
    LoadBalancer(HostSet& host_set, OriginalDstCluster& parent);

    // Upstream::LoadBalancer
    HostConstSharedPtr chooseHost(const LoadBalancerContext* context) override;

  private:
    HostSet& host_set_;
    OriginalDstCluster& parent_;
    std::unordered_multimap<std::string, HostSharedPtr> host_map_;
  };

private:
  HostSharedPtr createHost(Network::Address::InstanceConstSharedPtr);
  void addHost(HostSharedPtr);

  void cleanup();

  Event::Dispatcher& dispatcher_;
  const std::chrono::milliseconds cleanup_interval_ms_;
  Event::TimerPtr cleanup_timer_;
};

} // namespace Upstream
} // namespace Envoy
