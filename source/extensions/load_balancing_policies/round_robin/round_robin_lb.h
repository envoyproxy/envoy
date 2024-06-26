#pragma once

#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * A round robin load balancer. When in weighted mode, EDF scheduling is used. When in not
 * weighted mode, simple RR index selection is used.
 */
class RoundRobinLoadBalancer : public EdfLoadBalancerBase {
public:
  RoundRobinLoadBalancer(
      const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterLbStats& stats,
      Runtime::Loader& runtime, Random::RandomGenerator& random,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config,
      OptRef<const envoy::config::cluster::v3::Cluster::RoundRobinLbConfig> round_robin_config,
      TimeSource& time_source)
      : EdfLoadBalancerBase(
            priority_set, local_priority_set, stats, runtime, random,
            PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(common_config, healthy_panic_threshold,
                                                           100, 50),
            LoadBalancerConfigHelper::localityLbConfigFromCommonLbConfig(common_config),
            round_robin_config.has_value()
                ? LoadBalancerConfigHelper::slowStartConfigFromLegacyProto(round_robin_config.ref())
                : absl::nullopt,
            time_source) {
    initialize();
  }

  RoundRobinLoadBalancer(
      const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterLbStats& stats,
      Runtime::Loader& runtime, Random::RandomGenerator& random, uint32_t healthy_panic_threshold,
      const envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin&
          round_robin_config,
      TimeSource& time_source)
      : EdfLoadBalancerBase(
            priority_set, local_priority_set, stats, runtime, random, healthy_panic_threshold,
            LoadBalancerConfigHelper::localityLbConfigFromProto(round_robin_config),
            LoadBalancerConfigHelper::slowStartConfigFromProto(round_robin_config), time_source) {
    initialize();
  }

private:
  void refreshHostSource(const HostsSource& source) override {
    // insert() is used here on purpose so that we don't overwrite the index if the host source
    // already exists. Note that host sources will never be removed, but given how uncommon this
    // is it probably doesn't matter.
    rr_indexes_.insert({source, seed_});
    // If the list of hosts changes, the order of picks change. Discard the
    // index.
    peekahead_index_ = 0;
  }
  double hostWeight(const Host& host) const override {
    if (!noHostsAreInSlowStart()) {
      return applySlowStartFactor(host.weight(), host);
    }
    return host.weight();
  }

  HostConstSharedPtr unweightedHostPeek(const HostVector& hosts_to_use,
                                        const HostsSource& source) override {
    auto i = rr_indexes_.find(source);
    if (i == rr_indexes_.end()) {
      return nullptr;
    }
    return hosts_to_use[(i->second + (peekahead_index_)++) % hosts_to_use.size()];
  }

  HostConstSharedPtr unweightedHostPick(const HostVector& hosts_to_use,
                                        const HostsSource& source) override {
    if (peekahead_index_ > 0) {
      --peekahead_index_;
    }
    // To avoid storing the RR index in the base class, we end up using a second map here with
    // host source as the key. This means that each LB decision will require two map lookups in
    // the unweighted case. We might consider trying to optimize this in the future.
    ASSERT(rr_indexes_.find(source) != rr_indexes_.end());
    return hosts_to_use[rr_indexes_[source]++ % hosts_to_use.size()];
  }

  uint64_t peekahead_index_{};
  absl::flat_hash_map<HostsSource, uint64_t, HostsSourceHash> rr_indexes_;
};

} // namespace Upstream
} // namespace Envoy
