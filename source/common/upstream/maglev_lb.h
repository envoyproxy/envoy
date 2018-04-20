#pragma once

#include "common/upstream/thread_aware_lb_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * This is an implementation of Maglev consistent hashing as described in:
 * https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/44824.pdf
 * section 3.4. Specifically, the algorithm shown in pseudocode listening 1 is implemented
 * with a fixed table size of 65537. This is the recommended table size in section 5.3.
 */
class MaglevTable : public ThreadAwareLoadBalancerBase::HashingLoadBalancer,
                    Logger::Loggable<Logger::Id::upstream> {
public:
  MaglevTable(const HostsPerLocality& hosts_per_locality,
              const LocalityWeightsConstSharedPtr& locality_weights,
              uint64_t table_size = DefaultTableSize);

  // ThreadAwareLoadBalancerBase::HashingLoadBalancer
  HostConstSharedPtr chooseHost(uint64_t hash) const override;

  // Recommended table size in section 5.3 of the paper.
  static const uint64_t DefaultTableSize = 65537;

private:
  struct TableBuildEntry {
    TableBuildEntry(const HostSharedPtr& host, uint64_t offset, uint64_t skip, uint64_t weight)
        : host_(host), offset_(offset), skip_(skip), weight_(weight) {}

    HostSharedPtr host_;
    const uint64_t offset_;
    const uint64_t skip_;
    const uint64_t weight_;
    uint64_t counts_{};
    uint64_t next_{};
  };

  uint64_t permutation(const TableBuildEntry& entry);

  const uint64_t table_size_;
  HostVector table_;
};

/**
 * Thread aware load balancer implementation for Maglev.
 */
class MaglevLoadBalancer : public ThreadAwareLoadBalancerBase {
public:
  MaglevLoadBalancer(const PrioritySet& priority_set, ClusterStats& stats, Runtime::Loader& runtime,
                     Runtime::RandomGenerator& random,
                     const envoy::api::v2::Cluster::CommonLbConfig& common_config,
                     uint64_t table_size = MaglevTable::DefaultTableSize)
      : ThreadAwareLoadBalancerBase(priority_set, stats, runtime, random, common_config),
        table_size_(table_size) {}

private:
  // ThreadAwareLoadBalancerBase
  HashingLoadBalancerSharedPtr createLoadBalancer(const HostSet& host_set) override {
    // Note that we only compute global panic on host set refresh. Given that the runtime setting
    // will rarely change, this is a reasonable compromise to avoid creating extra LBs when we only
    // need to create one per priority level.
    const bool has_locality =
        host_set.localityWeights() != nullptr && !host_set.localityWeights()->empty();
    if (isGlobalPanic(host_set)) {
      if (!has_locality) {
        return std::make_shared<MaglevTable>(HostsPerLocalityImpl(host_set.hosts(), false), nullptr,
                                             table_size_);
      } else {
        return std::make_shared<MaglevTable>(host_set.hostsPerLocality(),
                                             host_set.localityWeights(), table_size_);
      }
    } else {
      if (!has_locality) {
        return std::make_shared<MaglevTable>(HostsPerLocalityImpl(host_set.healthyHosts(), false),
                                             nullptr, table_size_);
      } else {
        return std::make_shared<MaglevTable>(host_set.healthyHostsPerLocality(),
                                             host_set.localityWeights(), table_size_);
      }
    }
  }

  const uint64_t table_size_;
};

} // namespace Upstream
} // namespace Envoy
