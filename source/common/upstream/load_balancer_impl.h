#pragma once

#include <cstdint>
#include <vector>

#include "envoy/runtime/runtime.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "api/cds.pb.h"

namespace Envoy {
namespace Upstream {

/**
 * Utilities common to all load balancers.
 */
class LoadBalancerUtility {
public:
  /**
   * For the given host_set @return if we should be in a panic mode or not. For example, if the
   * majority of hosts are unhealthy we'll be likely in a panic mode. In this case we'll route
   * requests to hosts regardless of whether they are healthy or not.
   */
  static bool isGlobalPanic(const HostSet& host_set, Runtime::Loader& runtime);
};

/**
 * Base class for all LB implementations.
 */
class LoadBalancerBase {
protected:
  LoadBalancerBase(const HostSet& host_set, const HostSet* local_host_set, ClusterStats& stats,
                   Runtime::Loader& runtime, Runtime::RandomGenerator& random);
  ~LoadBalancerBase();

  /**
   * Pick the host list to use (healthy or all depending on how many in the set are not healthy).
   */
  const std::vector<HostSharedPtr>& hostsToUse();

  ClusterStats& stats_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;

private:
  enum class ZoneRoutingState { NoZoneRouting, ZoneDirect, ZoneResidual };

  /**
   * @return decision on quick exit from zone aware routing based on cluster configuration.
   * This gets recalculated on update callback.
   */
  bool earlyExitNonZoneRouting();

  /**
   * Try to select upstream hosts from the same zone.
   */
  const std::vector<HostSharedPtr>& tryChooseLocalZoneHosts();

  /**
   * @return (number of hosts in a given zone)/(total number of hosts) in ret param.
   * The result is stored as integer number and scaled by 10000 multiplier for better precision.
   * Caller is responsible for allocation/de-allocation of ret.
   */
  void calculateZonePercentage(const std::vector<std::vector<HostSharedPtr>>& hosts_per_zone,
                               uint64_t* ret);

  /**
   * Regenerate zone aware routing structures for fast decisions on upstream zone selection.
   */
  void regenerateZoneRoutingStructures();

  const HostSet& host_set_;
  const HostSet* local_host_set_;
  uint64_t local_percent_to_route_{};
  ZoneRoutingState zone_routing_state_{ZoneRoutingState::NoZoneRouting};
  std::vector<uint64_t> residual_capacity_;
  Common::CallbackHandle* local_host_set_member_update_cb_handle_{};
};

/**
 * Implementation of LoadBalancer that performs RR selection across the hosts in the cluster.
 */
class RoundRobinLoadBalancer : public LoadBalancer, LoadBalancerBase {
public:
  RoundRobinLoadBalancer(const HostSet& host_set, const HostSet* local_host_set_,
                         ClusterStats& stats, Runtime::Loader& runtime,
                         Runtime::RandomGenerator& random)
      : LoadBalancerBase(host_set, local_host_set_, stats, runtime, random) {}

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(const LoadBalancerContext* context) override;

private:
  size_t rr_index_{};
};

/**
 * Weighted Least Request load balancer.
 *
 * In a normal setup when all hosts have the same weight of 1 it randomly picks up two healthy hosts
 * and compares number of active requests.
 * Technique is based on http://www.eecs.harvard.edu/~michaelm/postscripts/mythesis.pdf
 *
 * When any of the hosts have non 1 weight, apply random weighted balancing.
 * Randomly pickup the host and send 'weight' number of requests to it.
 * This technique is acceptable for load testing but
 * will not work well in situations where requests take a long time.
 * In that case a different algorithm using a full scan will be required.
 */
class LeastRequestLoadBalancer : public LoadBalancer, LoadBalancerBase {
public:
  LeastRequestLoadBalancer(const HostSet& host_set, const HostSet* local_host_set_,
                           ClusterStats& stats, Runtime::Loader& runtime,
                           Runtime::RandomGenerator& random);

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(const LoadBalancerContext* context) override;

private:
  HostSharedPtr last_host_;
  uint32_t hits_left_{};
};

/**
 * Random load balancer that picks a random host out of all hosts.
 */
class RandomLoadBalancer : public LoadBalancer, LoadBalancerBase {
public:
  RandomLoadBalancer(const HostSet& host_set, const HostSet* local_host_set, ClusterStats& stats,
                     Runtime::Loader& runtime, Runtime::RandomGenerator& random)
      : LoadBalancerBase(host_set, local_host_set, stats, runtime, random) {}

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(const LoadBalancerContext* context) override;
};

class LoadBalancerSubsetInfoImpl : public LoadBalancerSubsetInfo {
public:
  LoadBalancerSubsetInfoImpl(const envoy::api::v2::Cluster::LbSubsetConfig& subset_config)
      : enabled_(subset_config.subset_keys_size() != 0),
        fallback_policy_(subset_config.fallback_policy()),
        default_subset_(subset_config.default_subset()) {
    for (auto subset : subset_config.subset_keys()) {
      if (subset.keys_size() > 0) {
        std::vector<std::string> v;
        for (auto key : subset.keys()) {
          v.push_back(key);
        }

        std::sort(v.begin(), v.end());
        subset_keys_.push_back(v);
      }
    }
  }

  bool isEnabled() const override { return enabled_; }

  envoy::api::v2::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy fallbackPolicy() const override {
    return fallback_policy_;
  }

  const ProtobufWkt::Struct& defaultSubset() const override { return default_subset_; }

  const std::vector<std::vector<std::string>>& subsetKeys() const override { return subset_keys_; }

private:
  bool enabled_;
  envoy::api::v2::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy fallback_policy_;
  ProtobufWkt::Struct default_subset_;
  std::vector<std::vector<std::string>> subset_keys_;
};

} // namespace Upstream
} // namespace Envoy
