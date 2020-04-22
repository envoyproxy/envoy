#include "common/upstream/maglev_lb.h"

#include "envoy/config/cluster/v3/cluster.pb.h"

namespace Envoy {
namespace Upstream {

MaglevTable::MaglevTable(const NormalizedHostWeightVector& normalized_host_weights,
                         double max_normalized_weight, uint64_t table_size,
                         bool use_hostname_for_hashing, MaglevLoadBalancerStats& stats)
    : table_size_(table_size), stats_(stats) {
  // TODO(mattklein123): The Maglev table must have a size that is a prime number for the algorithm
  // to work. Currently, the table size is not user configurable. In the future, if the table size
  // is made user configurable, we will need proper error checking that the user cannot configure a
  // size that is not prime (the result is going to be an infinite loop with some inputs which is
  // not good!).
  ASSERT(Primes::isPrime(table_size));

  // We can't do anything sensible with no hosts.
  if (normalized_host_weights.empty()) {
    return;
  }

  // Implementation of pseudocode listing 1 in the paper (see header file for more info).
  std::vector<TableBuildEntry> table_build_entries;
  table_build_entries.reserve(normalized_host_weights.size());
  for (const auto& host_weight : normalized_host_weights) {
    const auto& host = host_weight.first;
    const std::string& address =
        use_hostname_for_hashing ? host->hostname() : host->address()->asString();
    ASSERT(!address.empty());
    table_build_entries.emplace_back(host, HashUtil::xxHash64(address) % table_size_,
                                     (HashUtil::xxHash64(address, 1) % (table_size_ - 1)) + 1,
                                     host_weight.second);
  }

  table_.resize(table_size_);

  // Iterate through the table build entries as many times as it takes to fill up the table.
  uint64_t table_index = 0;
  for (uint32_t iteration = 1; table_index < table_size_; ++iteration) {
    for (uint64_t i = 0; i < table_build_entries.size() && table_index < table_size; i++) {
      TableBuildEntry& entry = table_build_entries[i];
      // To understand how target_weight_ and weight_ are used below, consider a host with weight
      // equal to max_normalized_weight. This would be picked on every single iteration. If it had
      // weight equal to max_normalized_weight / 3, then it would only be picked every 3 iterations,
      // etc.
      if (iteration * entry.weight_ < entry.target_weight_) {
        continue;
      }
      entry.target_weight_ += max_normalized_weight;
      uint64_t c = permutation(entry);
      while (table_[c] != nullptr) {
        entry.next_++;
        c = permutation(entry);
      }

      table_[c] = entry.host_;
      entry.next_++;
      entry.count_++;
      table_index++;
    }
  }

  uint64_t min_entries_per_host = table_size_;
  uint64_t max_entries_per_host = 0;
  for (const auto& entry : table_build_entries) {
    min_entries_per_host = std::min(entry.count_, min_entries_per_host);
    max_entries_per_host = std::max(entry.count_, max_entries_per_host);
  }
  stats_.min_entries_per_host_.set(min_entries_per_host);
  stats_.max_entries_per_host_.set(max_entries_per_host);

  if (ENVOY_LOG_CHECK_LEVEL(trace)) {
    for (uint64_t i = 0; i < table_.size(); i++) {
      ENVOY_LOG(trace, "maglev: i={} host={}", i,
                use_hostname_for_hashing ? table_[i]->hostname()
                                         : table_[i]->address()->asString());
    }
  }
}

HostConstSharedPtr MaglevTable::chooseHost(uint64_t hash, uint32_t attempt) const {
  if (table_.empty()) {
    return nullptr;
  }

  if (attempt > 0) {
    // If a retry host predicate is being applied, mutate the hash to choose an alternate host.
    // By using value with most bits set for the retry attempts, we achieve a larger change in
    // the hash, thereby reducing the likelihood that all retries are directed to a single host.
    hash ^= ~0ULL - attempt + 1;
  }

  return table_[hash % table_size_];
}

uint64_t MaglevTable::permutation(const TableBuildEntry& entry) {
  return (entry.offset_ + (entry.skip_ * entry.next_)) % table_size_;
}

MaglevLoadBalancer::MaglevLoadBalancer(
    const PrioritySet& priority_set, ClusterStats& stats, Stats::Scope& scope,
    Runtime::Loader& runtime, Runtime::RandomGenerator& random,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config, uint64_t table_size)
    : ThreadAwareLoadBalancerBase(priority_set, stats, runtime, random, common_config),
      scope_(scope.createScope("maglev_lb.")), stats_(generateStats(*scope_)),
      table_size_(table_size),
      use_hostname_for_hashing_(
          common_config.has_consistent_hashing_lb_config()
              ? common_config.consistent_hashing_lb_config().use_hostname_for_hashing()
              : false) {}

MaglevLoadBalancerStats MaglevLoadBalancer::generateStats(Stats::Scope& scope) {
  return {ALL_MAGLEV_LOAD_BALANCER_STATS(POOL_GAUGE(scope))};
}

} // namespace Upstream
} // namespace Envoy
