#pragma once

#include "envoy/runtime/runtime.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

namespace Upstream {

/**
 * Base class for all LB implementations.
 */
class LoadBalancerBase {
protected:
  LoadBalancerBase(const HostSet& host_set, const HostSet* local_host_set, ClusterStats& stats,
                   Runtime::Loader& runtime, Runtime::RandomGenerator& random);

  /**
   * Pick the host list to use (healthy or all depending on how many in the set are not healthy).
   */
  const std::vector<HostPtr>& hostsToUse();

  ClusterStats& stats_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;

private:
  /*
   * @return decision on quick exit from zone aware routing based on cluster configuration
   * or other non runtime params.
   */
  bool earlyExitNonZoneRouting();

  /*
   * @return decision on quick exit from zone aware routing based on runtime params.
   */
  bool earlyExitNonZoneRoutingRuntime();

  /**
   * For the given host_set it @return if we should be in a panic mode or not.
   * For example, if majority of hosts are unhealthy we'll be likely in a panic mode.
   * In this case we'll route requests to hosts no matter if they are healthy or not.
   */
  bool isGlobalPanic(const HostSet& host_set);

  /**
   * Try to select upstream hosts from the same zone.
   */
  const std::vector<HostPtr>& tryChooseLocalZoneHosts();

  /**
   * @return (number of hosts in a given zone)/(total number of hosts) in ret param.
   * The result is stored as integer number and scaled by 10000 multiplier for better precision.
   * Caller is responsible for allocation/de-allocation of ret.
   */
  void calculateZonePercentage(const std::vector<std::vector<HostPtr>>& hosts_per_zone,
                               uint64_t* ret);

  /**
   * Regenerate zone aware routing structures for fast decisions on upstream zone selection.
   */
  void regenerateZoneRoutingStructures();

  const HostSet& host_set_;
  const HostSet* local_host_set_;

  bool early_exit_zone_routing_;
  uint64_t local_percent_to_route_{};
  bool route_directly_{};
  std::vector<uint64_t> residual_capacity_;
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
  ConstHostPtr chooseHost() override;

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
  ConstHostPtr chooseHost() override;

private:
  HostPtr last_host_;
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
  ConstHostPtr chooseHost() override;
};

} // Upstream
