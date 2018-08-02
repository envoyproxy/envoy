#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "envoy/secret/secret_manager.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/empty_string.h"
#include "common/common/logger.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * The OriginalDstCluster is a dynamic cluster that automatically adds hosts as needed based on the
 * original destination address of the downstream connection. These hosts are also automatically
 * cleaned up after they have not seen traffic for a configurable cleanup interval time
 * ("cleanup_interval_ms").
 */
class OriginalDstCluster : public ClusterImplBase {
public:
  OriginalDstCluster(const envoy::api::v2::Cluster& config, Runtime::Loader& runtime,
                     Server::Configuration::TransportSocketFactoryContext& factory_context,
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
    LoadBalancer(PrioritySet& priority_set, ClusterSharedPtr& parent);

    // Upstream::LoadBalancer
    HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

  private:
    /**
     * Map from an host IP address/port to a HostSharedPtr. Due to races multiple distinct host
     * objects with the same address can be created, so we need to use a multimap.
     */
    class HostMap {
    public:
      bool insert(const HostSharedPtr& host, bool check = true) {
        if (check) {
          auto range = map_.equal_range(host->address()->asString());
          auto it = std::find_if(
              range.first, range.second,
              [&host](const decltype(map_)::value_type pair) { return pair.second == host; });
          if (it != range.second) {
            return false; // 'host' already in the map, no need to insert.
          }
        }
        map_.emplace(host->address()->asString(), host);
        return true;
      }

      void remove(const HostSharedPtr& host) {
        auto range = map_.equal_range(host->address()->asString());
        auto it =
            std::find_if(range.first, range.second, [&host](const decltype(map_)::value_type pair) {
              return pair.second == host;
            });
        ASSERT(it != range.second);
        map_.erase(it);
      }

      HostSharedPtr find(const Network::Address::Instance& address) {
        auto it = map_.find(address.asString());

        if (it != map_.end()) {
          return it->second;
        }
        return nullptr;
      }

    private:
      std::unordered_multimap<std::string, HostSharedPtr> map_;
    };

    Network::Address::InstanceConstSharedPtr requestOverrideHost(LoadBalancerContext* context);

    PrioritySet& priority_set_;                // Thread local priority set.
    std::weak_ptr<OriginalDstCluster> parent_; // Primary cluster managed by the main thread.
    ClusterInfoConstSharedPtr info_;
    HostMap host_map_;
  };

private:
  void addHost(HostSharedPtr&);
  void cleanup();

  // ClusterImplBase
  void startPreInit() override { onPreInitComplete(); }

  Event::Dispatcher& dispatcher_;
  const std::chrono::milliseconds cleanup_interval_ms_;
  Event::TimerPtr cleanup_timer_;
};

} // namespace Upstream
} // namespace Envoy
