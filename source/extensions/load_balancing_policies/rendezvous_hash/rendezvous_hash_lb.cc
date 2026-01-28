#include "source/extensions/load_balancing_policies/rendezvous_hash/rendezvous_hash_lb.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/common/assert.h"

#include "absl/numeric/bits.h"

namespace Envoy {
namespace Upstream {

namespace {
// Mask to extract lower 53 bits (fits in double mantissa precision)
constexpr uint64_t MASK_53 = (1ULL << 53) - 1;
// Scale factor to convert to [0.0, 1.0)
constexpr double SCALE_53 = 1.0 / (1ULL << 53);
// Mask to extract mantissa bits (bits 0-51)
constexpr uint64_t MANTISSA_MASK = (1ULL << 52) - 1;
// Mask to extract exponent bits (bits 52-62)
constexpr uint64_t EXPONENT_MASK = 0x7FFULL << 52;
// ln(2) constant for log conversion
constexpr double LN_2 = 0.6931471805599453;
// Xorshift* multiplier constant
constexpr uint64_t XORSHIFT_MULT = 2685821657736338717ULL;
} // namespace

TypedRendezvousHashLbConfig::TypedRendezvousHashLbConfig(const RendezvousHashLbProto& lb_config,
                                                         Regex::Engine& regex_engine,
                                                         absl::Status& creation_status)
    : TypedHashLbConfigBase(lb_config.consistent_hashing_lb_config().hash_policy(), regex_engine,
                            creation_status),
      lb_config_(lb_config) {}

RendezvousHashLoadBalancer::RendezvousHashLoadBalancer(
    const PrioritySet& priority_set, ClusterLbStats& stats, Stats::Scope& scope,
    Runtime::Loader& runtime, Random::RandomGenerator& random, uint32_t healthy_panic_threshold,
    const RendezvousHashLbProto& config, HashPolicySharedPtr hash_policy)
    : ThreadAwareLoadBalancerBase(priority_set, stats, runtime, random, healthy_panic_threshold,
                                  config.has_locality_weighted_lb_config(), std::move(hash_policy)),
      scope_(scope.createScope("rendezvous_hash_lb.")),
      use_hostname_for_hashing_(
          config.consistent_hashing_lb_config().use_hostname_for_hashing()),
      hash_balance_factor_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.consistent_hashing_lb_config(),
                                                           hash_balance_factor, 0)) {}

ThreadAwareLoadBalancerBase::HashingLoadBalancerSharedPtr
RendezvousHashLoadBalancer::createLoadBalancer(
    const NormalizedHostWeightVector& normalized_host_weights, double /* min_normalized_weight */,
    double /* max_normalized_weight */) {
  HashingLoadBalancerSharedPtr rendezvous_lb = std::make_shared<RendezvousHashTable>(
      normalized_host_weights, use_hostname_for_hashing_);

  if (hash_balance_factor_ == 0) {
    return rendezvous_lb;
  }

  return std::make_shared<BoundedLoadHashingLoadBalancer>(
      rendezvous_lb, std::move(normalized_host_weights), hash_balance_factor_);
}

RendezvousHashLoadBalancer::RendezvousHashTable::RendezvousHashTable(
    const NormalizedHostWeightVector& normalized_host_weights, bool use_hostname_for_hashing) {
  ENVOY_LOG(trace, "rendezvous hash: building table with {} hosts",
            normalized_host_weights.size());

  // We can't do anything sensible with no hosts.
  if (normalized_host_weights.empty()) {
    return;
  }

  hosts_.reserve(normalized_host_weights.size());

  for (const auto& entry : normalized_host_weights) {
    const auto& host = entry.first;
    const absl::string_view key_to_hash = hashKey(host, use_hostname_for_hashing);
    ASSERT(!key_to_hash.empty());

    // Compute hash once per host and store it
    const uint64_t host_hash = HashUtil::xxHash64(key_to_hash);
    hosts_.emplace_back(host, host_hash, entry.second);

    ENVOY_LOG(trace, "rendezvous hash: host={} hash={} weight={}", key_to_hash, host_hash,
              entry.second);
  }
}

HostSelectionResponse
RendezvousHashLoadBalancer::RendezvousHashTable::chooseHost(uint64_t hash,
                                                            uint32_t attempt) const {
  if (hosts_.empty()) {
    return {nullptr};
  }

  // For retries, mutate the hash to get a different host
  if (attempt > 0) {
    hash ^= ~0ULL - attempt + 1;
  }

  // Find the host with the highest score
  size_t max_index = 0;
  double max_score = computeScore(hash, hosts_[0].hash_, hosts_[0].weight_);

  for (size_t i = 1; i < hosts_.size(); i++) {
    const double score = computeScore(hash, hosts_[i].hash_, hosts_[i].weight_);
    if (score > max_score) {
      max_score = score;
      max_index = i;
    }
  }

  return {hosts_[max_index].host_};
}

double RendezvousHashLoadBalancer::RendezvousHashTable::computeScore(uint64_t key,
                                                                      uint64_t host_hash,
                                                                      double weight) {
  // Combine the key and host hash using xorshift*
  const uint64_t combined_hash = xorshiftMult64(key ^ host_hash);
  // Normalize to [0.0, 1.0)
  const double normalized = normalizeHash(combined_hash);
  // Compute weighted score: -weight / ln(x)
  return -weight / fastLog(normalized);
}

uint64_t RendezvousHashLoadBalancer::RendezvousHashTable::xorshiftMult64(uint64_t x) {
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  return x * XORSHIFT_MULT;
}

double RendezvousHashLoadBalancer::RendezvousHashTable::normalizeHash(uint64_t hash) {
  return static_cast<double>(hash & MASK_53) * SCALE_53;
}

double RendezvousHashLoadBalancer::RendezvousHashTable::fastLog(double x) {
  return fastLog2(x) * LN_2;
}

double RendezvousHashLoadBalancer::RendezvousHashTable::fastLog2(double x) {
  uint64_t bits;
  std::memcpy(&bits, &x, sizeof(double));

  // Extract exponent (biased by +1023 in IEEE 754) and mantissa
  const uint64_t exponent = (bits & EXPONENT_MASK) >> 52;
  const uint64_t mantissa = bits & MANTISSA_MASK;

  // Calculate new exponent to shift mantissa into range [0.75, 1.5)
  // If the leftmost mantissa bit is set (value > 1.5), reduce exponent by 1
  const uint64_t new_exponent = 1023ULL ^ (mantissa >> 51);

  // Reconstruct double with new exponent
  const uint64_t new_bits = (new_exponent << 52) | mantissa;
  double new_double;
  std::memcpy(&new_double, &new_bits, sizeof(double));

  // Extract the true exponent
  const double base_exponent = static_cast<double>(exponent) - static_cast<double>(new_exponent);

  // Rational polynomial approximation of log2(1+t) for t in [-0.25, 0.5)
  const double t = new_double - 1.0;
  const double approx = t * (0.338953 * t + 2.198599) / (t + 1.523692);

  // log2(m * 2^e) = log2(m) + e
  return base_exponent + approx;
}

} // namespace Upstream
} // namespace Envoy
