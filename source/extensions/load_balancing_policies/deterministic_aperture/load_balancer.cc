#include "source/extensions/load_balancing_policies/deterministic_aperture/load_balancer.h"

#include "envoy/config/cluster/v3/cluster.pb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DeterministicAperture {

Upstream::Ring::HashFunction
LoadBalancer::toRingHashFunction(const ProtoHashFunction& hash_func) const {
  switch (hash_func) {
  case ProtoHashFunction::DeterministicApertureLbConfig_HashFunction_XX_HASH:
    return Upstream::Ring::HashFunction::XX_HASH;
  case ProtoHashFunction::DeterministicApertureLbConfig_HashFunction_MURMUR_HASH_2:
    return Upstream::Ring::HashFunction::MURMUR_HASH_2;
  default:
    throw EnvoyException("Unsupported hash function");
  }
}

LoadBalancer::LoadBalancer(
    const Upstream::PrioritySet& priority_set, Upstream::ClusterLbStats& stats, Stats::Scope& scope,
    Runtime::Loader& runtime, Random::RandomGenerator& random,
    const absl::optional<envoy::extensions::load_balancing_policies::deterministic_aperture::v3::
                             DeterministicApertureLbConfig>& config,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
    : ThreadAwareLoadBalancerBase(priority_set, stats, runtime, random, common_config),
      min_ring_size_(config ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value(), minimum_ring_size,
                                                              Upstream::Ring::DefaultMinRingSize)
                            : Upstream::Ring::DefaultMinRingSize),
      max_ring_size_(config ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value(), maximum_ring_size,
                                                              Upstream::Ring::DefaultMaxRingSize)
                            : Upstream::Ring::DefaultMaxRingSize),
      hash_function_(toRingHashFunction(
          config ? config.value().hash_function()
                 : ProtoHashFunction::DeterministicApertureLbConfig_HashFunction_XX_HASH)),
      use_hostname_for_hashing_(
          common_config.has_consistent_hashing_lb_config()
              ? common_config.consistent_hashing_lb_config().use_hostname_for_hashing()
              : false),
      hash_balance_factor_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          common_config.consistent_hashing_lb_config(), hash_balance_factor, 0)),
      width_((config.has_value() && config->total_peers() > 0) ? (1.0 / config->total_peers())
                                                               : 1.0),
      offset_((config.has_value() && width_ > 0.0) ? (width_ * config->peer_index()) : 0.0),
      scope_(scope.createScope("deterministic_aperture_lb.")),
      ring_stats_(Upstream::Ring::generateStats(*scope_)) {}

LoadBalancerStats LoadBalancer::Ring::generateStats(Stats::Scope& scope) {
  return {ALL_DETERMINISTIC_APERTURE_LOAD_BALANCER_STATS(POOL_COUNTER(scope))};
}

/*
 * TODO(jojy): The concept of HashingLoadBalancer::chooseHost might not directly apply here since we
 * don't actually use the hash of the nodes placed in the ring. We use `random` algorithm in the
 * Deterministic aperture load balancer. Hence we ignore the `h` and `attempt` here.
 */
Upstream::HostConstSharedPtr LoadBalancer::Ring::chooseHost(uint64_t, uint32_t) const {
  if (ring_.empty()) {
    return nullptr;
  }

  const std::pair<size_t, size_t> index_pair = pick2();

  const Upstream::Ring::Entry& first = ring_[index_pair.first];
  const Upstream::Ring::Entry& second = ring_[index_pair.second];

  ENVOY_LOG(debug, "pick2 returned hosts: (hash1: {}, address1: {}, hash2: {}, address2: {})",
            first.hash_, first.host_->address()->asString(), second.hash_,
            second.host_->address()->asString());

  return first.host_->stats().rq_active_.value() < second.host_->stats().rq_active_.value()
             ? first.host_
             : second.host_;
}

using HashFunction = envoy::config::cluster::v3::Cluster::RingHashLbConfig::HashFunction;
LoadBalancer::Ring::Ring(double offset, double width,
                         const Upstream::NormalizedHostWeightVector& normalized_host_weights,
                         double min_normalized_weight, uint64_t min_ring_size,
                         uint64_t max_ring_size, HashFunction hash_function,
                         bool use_hostname_for_hashing, Stats::ScopeSharedPtr scope,
                         Upstream::RingStats ring_stats)
    : Upstream::Ring(normalized_host_weights, min_normalized_weight, min_ring_size, max_ring_size,
                     hash_function, use_hostname_for_hashing, ring_stats),
      offset_(offset), width_(width), unit_width_(1.0 / ring_size_), rng_(random_dev_()),
      random_distribution_(0, 1), stats_(generateStats(*scope)) {
  if (width_ > 1.0 || width_ < 0) {
    throw EnvoyException(
        fmt::format("Invalid width for the deterministic aperture ring{}", width_));
  }
}

absl::optional<double> LoadBalancer::Ring::weight(size_t index, double offset, double width) const {
  if (index >= ring_size_ || width > 1 || offset > 1) {
    return absl::nullopt;
  }

  double index_begin = index * unit_width_;
  double index_end = index_begin + unit_width_;

  if (offset + width > 1) {
    double start = std::fmod((offset + width), 1.0);

    return 1.0 - (intersect(index_begin, index_end, start, offset)) / unit_width_;
  }

  return intersect(index_begin, index_end, offset, offset + width) / unit_width_;
}

size_t LoadBalancer::Ring::getIndex(double offset) const {
  ASSERT(offset >= 0 && offset <= 1, "valid offset");
  return offset / unit_width_;
}

double LoadBalancer::Ring::intersect(double b0, double e0, double b1, double e1) const {
  ENVOY_LOG(trace, "Overlap for (b0: {}, e0: {}, b1: {}, e1: {})", b0, e0, b1, e1);
  return std::max(0.0, std::min(e0, e1) - std::max(b0, b1));
}

size_t LoadBalancer::Ring::pick() const {
  return getIndex(std::fmod((offset_ + width_ * nextRandom()), 1.0));
}

size_t LoadBalancer::Ring::pickSecond(size_t first) const {
  double f_begin = first * unit_width_;
  ENVOY_LOG(trace, "Pick second for (first: {}, offset: {}, width: {}, first begin: {})", first,
            offset_, width_, f_begin);

  if (f_begin + 1 < offset_ + width_) {
    f_begin = f_begin + 1;
    ENVOY_LOG(trace, "Adjusted first begin to : {}", f_begin);
  }

  double f_end = f_begin + unit_width_;

  double overlap = intersect(f_begin, f_end, offset_, offset_ + width_);
  double rem = width_ - overlap;

  if (rem <= 0) {
    return first;
  }

  double pos = offset_ + (nextRandom() * rem);
  ENVOY_LOG(trace, "Overlap: {}, remainder: {}, second offset: {}", overlap, rem, pos);

  if (pos >= (f_end - overlap)) {
    pos += overlap;
    ENVOY_LOG(trace, "Adjusted second offset to: {}", pos);
  }

  return getIndex(std::fmod(pos, 1.0));
}

std::pair<size_t, size_t> LoadBalancer::Ring::pick2() const {
  ENVOY_LOG(trace, "pick2 for offset: {}, width: {}", offset_, width_);
  const size_t first = pick();
  const size_t second = pickSecond(first);

  if (first == second) {
    stats_.pick2_same_.inc();
  }

  ENVOY_LOG(trace, "Returning: ({}, {})", first, second);
  return {first, second};
}

Upstream::ThreadAwareLoadBalancerPtr
LoadBalancerFactory::create(const Upstream::ClusterInfo& cluster_info,
                            const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
                            Random::RandomGenerator& random, TimeSource& time_source) {
  (void)time_source;

  const auto* typed_config =
      dynamic_cast<const envoy::extensions::load_balancing_policies::deterministic_aperture::v3::
                       DeterministicApertureLbConfig*>(cluster_info.loadBalancingPolicy().get());
  RELEASE_ASSERT(
      typed_config != nullptr,
      "Invalid load balancing policy configuration for deterministic aperture load balancer");

  return std::make_unique<LoadBalancer>(priority_set, cluster_info.lbStats(),
                                        cluster_info.statsScope(), runtime, random, *typed_config,
                                        cluster_info.lbConfig());
}

REGISTER_FACTORY(LoadBalancerFactory, Upstream::TypedLoadBalancerFactory);

} // namespace DeterministicAperture
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
