#include "source/extensions/load_balancing_policies/maglev/maglev_lb.h"

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Upstream {
namespace {
bool shouldUseCompactTable(size_t num_hosts, uint64_t table_size) {
  // Don't use compact maglev on 32-bit platforms.
  if constexpr (!(ENVOY_BIT_ARRAY_SUPPORTED)) {
    return false;
  }

#ifdef MAGLEV_LB_FORCE_ORIGINAL_IMPL
  return false;
#endif

  if (num_hosts > MaglevTable::MaxNumberOfHostsForCompactMaglev) {
    return false;
  }

  constexpr size_t shared_ptr_size = sizeof(HostConstSharedPtr);
  const uint64_t original_maglev_cost = shared_ptr_size * table_size;
  // We might be off by a byte e.g. due to rounding down when going from bits to
  // bytes.
  const uint64_t compact_maglev_cost =
      shared_ptr_size * num_hosts + ((absl::bit_width(num_hosts) * table_size) / 8);
  return compact_maglev_cost < original_maglev_cost;
}

/**
 * Factory for creating the optimal Maglev table instance for the given parameters.
 */
class MaglevFactory : private Logger::Loggable<Logger::Id::upstream> {
public:
  static MaglevTableSharedPtr
  createMaglevTable(const NormalizedHostWeightVector& normalized_host_weights,
                    double max_normalized_weight, uint64_t table_size,
                    bool use_hostname_for_hashing, MaglevLoadBalancerStats& stats) {

    MaglevTableSharedPtr maglev_table;
    if (shouldUseCompactTable(normalized_host_weights.size(), table_size)) {
      maglev_table =
          std::make_shared<CompactMaglevTable>(normalized_host_weights, max_normalized_weight,
                                               table_size, use_hostname_for_hashing, stats);
      ENVOY_LOG(debug, "creating compact maglev table given table size {} and number of hosts {}",
                table_size, normalized_host_weights.size());
    } else {
      maglev_table =
          std::make_shared<OriginalMaglevTable>(normalized_host_weights, max_normalized_weight,
                                                table_size, use_hostname_for_hashing, stats);
      ENVOY_LOG(debug, "creating original maglev table given table size {} and number of hosts {}",
                table_size, normalized_host_weights.size());
    }

    return maglev_table;
  }
};

} // namespace

LegacyMaglevLbConfig::LegacyMaglevLbConfig(const ClusterProto& cluster) {
  if (cluster.has_maglev_lb_config()) {
    lb_config_ = cluster.maglev_lb_config();
  }
}

TypedMaglevLbConfig::TypedMaglevLbConfig(const MaglevLbProto& lb_config) : lb_config_(lb_config) {}

ThreadAwareLoadBalancerBase::HashingLoadBalancerSharedPtr
MaglevLoadBalancer::createLoadBalancer(const NormalizedHostWeightVector& normalized_host_weights,
                                       double /* min_normalized_weight */,
                                       double max_normalized_weight) {
  HashingLoadBalancerSharedPtr maglev_lb =
      MaglevFactory::createMaglevTable(normalized_host_weights, max_normalized_weight, table_size_,
                                       use_hostname_for_hashing_, stats_);

  if (hash_balance_factor_ == 0) {
    return maglev_lb;
  }

  return std::make_shared<BoundedLoadHashingLoadBalancer>(
      maglev_lb, std::move(normalized_host_weights), hash_balance_factor_);
}

void MaglevTable::constructMaglevTableInternal(
    const NormalizedHostWeightVector& normalized_host_weights, double max_normalized_weight,
    bool use_hostname_for_hashing) {
  // We can't do anything sensible with no hosts.
  if (normalized_host_weights.empty()) {
    ENVOY_LOG(debug, "maglev: normalized hosts weights is empty, skipping building table");
    return;
  }

  // Prepare stable (sorted) vector of host_weight.
  // Maglev requires stable order of table_build_entries because the hash table will be filled in
  // the order. Unstable table_build_entries results the change of backend assignment.
  std::vector<std::tuple<absl::string_view, HostConstSharedPtr, double>> sorted_host_weights;
  sorted_host_weights.reserve(normalized_host_weights.size());
  for (const auto& host_weight : normalized_host_weights) {
    const auto& host = host_weight.first;
    const absl::string_view key_to_hash = hashKey(host, use_hostname_for_hashing);
    ASSERT(!key_to_hash.empty());

    sorted_host_weights.emplace_back(key_to_hash, host_weight.first, host_weight.second);
  }

  std::sort(sorted_host_weights.begin(), sorted_host_weights.end());

  // Implementation of pseudocode listing 1 in the paper (see header file for more info).
  std::vector<TableBuildEntry> table_build_entries;
  table_build_entries.reserve(normalized_host_weights.size());
  for (const auto& sorted_host_weight : sorted_host_weights) {
    const auto& key_to_hash = std::get<0>(sorted_host_weight);
    const auto& host = std::get<1>(sorted_host_weight);
    const auto& weight = std::get<2>(sorted_host_weight);

    table_build_entries.emplace_back(host, HashUtil::xxHash64(key_to_hash) % table_size_,
                                     (HashUtil::xxHash64(key_to_hash, 1) % (table_size_ - 1)) + 1,
                                     weight);
  }

  constructImplementationInternals(table_build_entries, max_normalized_weight);

  // Update Stats
  uint64_t min_entries_per_host = table_size_;
  uint64_t max_entries_per_host = 0;
  for (const auto& entry : table_build_entries) {
    min_entries_per_host = std::min(entry.count_, min_entries_per_host);
    max_entries_per_host = std::max(entry.count_, max_entries_per_host);
  }
  stats_.min_entries_per_host_.set(min_entries_per_host);
  stats_.max_entries_per_host_.set(max_entries_per_host);

  if (ENVOY_LOG_CHECK_LEVEL(trace)) {
    logMaglevTable(use_hostname_for_hashing);
  }
}

void OriginalMaglevTable::constructImplementationInternals(
    std::vector<TableBuildEntry>& table_build_entries, double max_normalized_weight) {
  // Size internal representation for maglev table correctly.
  table_.resize(table_size_);

  // Iterate through the table build entries as many times as it takes to fill up the table.
  uint64_t table_index = 0;
  for (uint32_t iteration = 1; table_index < table_size_; ++iteration) {
    for (uint64_t i = 0; i < table_build_entries.size() && table_index < table_size_; i++) {
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
}
CompactMaglevTable::CompactMaglevTable(const NormalizedHostWeightVector& normalized_host_weights,
                                       double max_normalized_weight, uint64_t table_size,
                                       bool use_hostname_for_hashing,
                                       MaglevLoadBalancerStats& stats)
    : MaglevTable(table_size, stats),
      table_(absl::bit_width(normalized_host_weights.size()), table_size) {
  constructMaglevTableInternal(normalized_host_weights, max_normalized_weight,
                               use_hostname_for_hashing);
}

void CompactMaglevTable::constructImplementationInternals(
    std::vector<TableBuildEntry>& table_build_entries, double max_normalized_weight) {
  // Populate the host table. Index into table_build_entries[i] will align with
  // the host here.
  host_table_.reserve(table_build_entries.size());
  for (const auto& entry : table_build_entries) {
    host_table_.emplace_back(entry.host_);
  }
  host_table_.shrink_to_fit();

  // Vector to track whether or not a given fixed width bit is set in the
  // BitArray used as the maglev table.
  std::vector<bool> occupied(table_size_, false);

  // Iterate through the table build entries as many times as it takes to fill up the table.
  uint64_t table_index = 0;
  for (uint32_t iteration = 1; table_index < table_size_; ++iteration) {
    for (uint32_t i = 0; i < table_build_entries.size() && table_index < table_size_; i++) {
      TableBuildEntry& entry = table_build_entries[i];
      // To understand how target_weight_ and weight_ are used below, consider a host with weight
      // equal to max_normalized_weight. This would be picked on every single iteration. If it had
      // weight equal to max_normalized_weight / 3, then it would only be picked every 3 iterations,
      // etc.
      if (iteration * entry.weight_ < entry.target_weight_) {
        continue;
      }
      entry.target_weight_ += max_normalized_weight;
      // As we're using the compact implementation, our table size is limited to
      // 32-bit, hence static_cast here should be safe.
      uint32_t c = static_cast<uint32_t>(permutation(entry));
      while (occupied[c]) {
        entry.next_++;
        c = static_cast<uint32_t>(permutation(entry));
      }

      // Record the index of the given host.
      table_.set(c, i);
      occupied[c] = true;

      entry.next_++;
      entry.count_++;
      table_index++;
    }
  }
}

void OriginalMaglevTable::logMaglevTable(bool use_hostname_for_hashing) const {
  for (uint64_t i = 0; i < table_.size(); ++i) {
    const absl::string_view key_to_hash = hashKey(table_[i], use_hostname_for_hashing);
    ENVOY_LOG(trace, "maglev: i={} address={} host={}", i, table_[i]->address()->asString(),
              key_to_hash);
  }
}

void CompactMaglevTable::logMaglevTable(bool use_hostname_for_hashing) const {
  for (uint64_t i = 0; i < table_size_; ++i) {

    const uint32_t index = table_.get(i);
    ASSERT(index < host_table_.size(), "Compact MaglevTable index into host table out of range");
    const auto& host = host_table_[index];

    const absl::string_view key_to_hash = hashKey(host, use_hostname_for_hashing);
    ENVOY_LOG(trace, "maglev: i={} address={} host={}", i, host->address()->asString(),
              key_to_hash);
  }
}

MaglevTable::MaglevTable(uint64_t table_size, MaglevLoadBalancerStats& stats)
    : table_size_(table_size), stats_(stats) {}

HostConstSharedPtr OriginalMaglevTable::chooseHost(uint64_t hash, uint32_t attempt) const {
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

HostConstSharedPtr CompactMaglevTable::chooseHost(uint64_t hash, uint32_t attempt) const {
  if (host_table_.empty()) {
    return nullptr;
  }

  if (attempt > 0) {
    // If a retry host predicate is being applied, mutate the hash to choose an alternate host.
    // By using value with most bits set for the retry attempts, we achieve a larger change in
    // the hash, thereby reducing the likelihood that all retries are directed to a single host.
    hash ^= ~0ULL - attempt + 1;
  }

  const uint32_t index = table_.get(hash % table_size_);
  ASSERT(index < host_table_.size(), "Compact MaglevTable index into host table out of range");
  return host_table_[index];
}

uint64_t MaglevTable::permutation(const TableBuildEntry& entry) {
  return (entry.offset_ + (entry.skip_ * entry.next_)) % table_size_;
}

MaglevLoadBalancer::MaglevLoadBalancer(
    const PrioritySet& priority_set, ClusterLbStats& stats, Stats::Scope& scope,
    Runtime::Loader& runtime, Random::RandomGenerator& random,
    OptRef<const envoy::config::cluster::v3::Cluster::MaglevLbConfig> config,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
    : ThreadAwareLoadBalancerBase(priority_set, stats, runtime, random,
                                  PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
                                      common_config, healthy_panic_threshold, 100, 50),
                                  common_config.has_locality_weighted_lb_config()),
      scope_(scope.createScope("maglev_lb.")), stats_(generateStats(*scope_)),
      table_size_(config ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.ref(), table_size,
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

MaglevLoadBalancer::MaglevLoadBalancer(
    const PrioritySet& priority_set, ClusterLbStats& stats, Stats::Scope& scope,
    Runtime::Loader& runtime, Random::RandomGenerator& random, uint32_t healthy_panic_threshold,
    const envoy::extensions::load_balancing_policies::maglev::v3::Maglev& config)
    : ThreadAwareLoadBalancerBase(priority_set, stats, runtime, random, healthy_panic_threshold,
                                  config.has_locality_weighted_lb_config()),
      scope_(scope.createScope("maglev_lb.")), stats_(generateStats(*scope_)),
      table_size_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, table_size, MaglevTable::DefaultTableSize)),
      use_hostname_for_hashing_(
          config.has_consistent_hashing_lb_config()
              ? config.consistent_hashing_lb_config().use_hostname_for_hashing()
              : false),
      hash_balance_factor_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.consistent_hashing_lb_config(),
                                                           hash_balance_factor, 0)) {
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
