#include "source/common/upstream/maglev_lb.h"

#include "envoy/config/cluster/v3/cluster.pb.h"

namespace Envoy {
namespace Upstream {

MaglevTable::MaglevTable(const NormalizedHostWeightVector& normalized_host_weights,
                         double max_normalized_weight, uint64_t table_size,
                         bool use_hostname_for_hashing, MaglevLoadBalancerStats& stats)
    : table_size_(table_size), stats_(stats) {
  // We can't do anything sensible with no hosts.
  if (normalized_host_weights.empty()) {
    ENVOY_LOG(debug, "maglev: normalized hosts weights is empty, skipping building table");
    return;
  }

  // Implementation of pseudocode listing 1 in the paper (see header file for more info).
  std::vector<TableBuildEntry> table_build_entries;
  table_build_entries.reserve(normalized_host_weights.size());
  for (const auto& host_weight : normalized_host_weights) {
    const auto& host = host_weight.first;
    const absl::string_view key_to_hash = hashKey(host, use_hostname_for_hashing);
    ASSERT(!key_to_hash.empty());
    table_build_entries.emplace_back(host, HashUtil::xxHash64(key_to_hash) % table_size_,
                                     (HashUtil::xxHash64(key_to_hash, 1) % (table_size_ - 1)) + 1,
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
      const absl::string_view key_to_hash = hashKey(table_[i], use_hostname_for_hashing);
      ENVOY_LOG(trace, "maglev: i={} address={} host={}", i, table_[i]->address()->asString(),
                key_to_hash);
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
    const PrioritySet& priority_set, ClusterLbStats& stats, Stats::Scope& scope,
    Runtime::Loader& runtime, Random::RandomGenerator& random,
    const absl::optional<envoy::config::cluster::v3::Cluster::MaglevLbConfig>& config,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
    : ThreadAwareLoadBalancerBase(priority_set, stats, runtime, random, common_config),
      scope_(scope.createScope("maglev_lb.")), stats_(generateStats(*scope_)),
      table_size_(config ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value(), table_size,
                                                           MaglevTable::DefaultTableSize)
                         : MaglevTable::DefaultTableSize),
      use_hostname_for_hashing_(
          common_config.has_consistent_hashing_lb_config()
              ? common_config.consistent_hashing_lb_config().use_hostname_for_hashing()
              : false),
      hash_balance_factor_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          common_config.consistent_hashing_lb_config(), hash_balance_factor, 0)) {
  ENVOY_LOG(debug, "maglev table size: {}", table_size_);
  // The table size must be prime number.
  if (!Primes::isPrime(table_size_)) {
    throw EnvoyException("The table size of maglev must be prime number");
  }
}

MaglevLoadBalancerStats MaglevLoadBalancer::generateStats(Stats::Scope& scope) {
  return {ALL_MAGLEV_LOAD_BALANCER_STATS(POOL_GAUGE(scope))};
}

} // namespace Upstream
} // namespace Envoy
