#pragma once

#include "common/upstream/thread_aware_lb_impl.h"

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
  MaglevTable(const HostVector& hosts, uint64_t table_size = DefaultTableSize);

  // ThreadAwareLoadBalancerBase::HashingLoadBalancer
  HostConstSharedPtr chooseHost(uint64_t hash) const override;

  // Recommended table size in section 5.3 of the paper.
  static const uint64_t DefaultTableSize = 65537;

private:
  struct TableBuildEntry {
    TableBuildEntry(uint64_t offset, uint64_t skip) : offset_(offset), skip_(skip) {}

    const uint64_t offset_;
    const uint64_t skip_;
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
  HashingLoadBalancerSharedPtr createLoadBalancer(const HostVector& hosts) override {
    return std::make_shared<MaglevTable>(hosts, table_size_);
  }

  const uint64_t table_size_;
};

} // namespace Upstream
} // namespace Envoy
