#include "source/common/upstream/deterministic_aperture_lb.h"

#include "envoy/config/cluster/v3/cluster.pb.h"

namespace Envoy {
namespace Upstream {

DeterministicApertureLoadBalancer::DeterministicApertureLoadBalancer(
    const PrioritySet& priority_set, ClusterStats& stats, Stats::Scope& scope,
    Runtime::Loader& runtime, Random::RandomGenerator& random,
    const absl::optional<envoy::config::cluster::v3::Cluster::DeterministicApertureLbConfig>&
        config,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
    : RingHashLoadBalancer(
          priority_set, stats, scope, runtime, random,
          (config.has_value()
               ? absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig>(
                     config->ring_config())
               : absl::nullopt),
          common_config),
      width_((config.has_value() && config->total_peers() > 0) ? (1.0 / config->total_peers())
                                                               : 1.0),
      offset_((config.has_value() && width_ > 0.0) ? (width_ * config->peer_index()) : 0.0),
      scope_(scope.createScope("deterministic_aperture_lb.")), stats_(generateStats(*scope_)),
      ring_stats_(RingHashLoadBalancer::generateStats(*scope_)) {}

DeterministicApertureLoadBalancerStats
DeterministicApertureLoadBalancer::generateStats(Stats::Scope& scope) {
  return {ALL_DETERMINISTIC_APERTURE_LOAD_BALANCER_STATS(POOL_COUNTER(scope))};
}

/*
 * TODO(jojy): The concept of HashingLoadBalancer::chooseHost might not directly apply here since we
 * don't actually use the hash of the nodes placed in the ring. We use `random` algorithm in the
 * Deterministic aperture load balancer. Hence we ignore the `h` and `attempt` here.
 */
HostConstSharedPtr DeterministicApertureLoadBalancer::Ring::chooseHost(uint64_t, uint32_t) const {
  if (ring_.empty()) {
    return nullptr;
  }

  absl::optional<std::pair<size_t, size_t>> maybe_index_pair = pick2();
  if (!maybe_index_pair) {
    return nullptr;
  }

  const RingHashLoadBalancer::RingEntry& first = ring_[maybe_index_pair->first];
  const RingHashLoadBalancer::RingEntry& second = ring_[maybe_index_pair->second];

  ENVOY_LOG(debug, "pick2 returned hosts: (hash1: {}, address1: {}, hash2: {}, address2: {})",
            first.hash_, first.host_->address()->asString(), second.hash_,
            second.host_->address()->asString());

  return first.host_->stats().rq_active_.value() < second.host_->stats().rq_active_.value()
             ? first.host_
             : second.host_;
}

using HashFunction = envoy::config::cluster::v3::Cluster::RingHashLbConfig::HashFunction;
DeterministicApertureLoadBalancer::Ring::Ring(
    double offset, double width, const NormalizedHostWeightVector& normalized_host_weights,
    double min_normalized_weight, uint64_t min_ring_size, uint64_t max_ring_size,
    HashFunction hash_function, bool use_hostname_for_hashing,
    RingHashLoadBalancerStats ring_hash_stats, DeterministicApertureLoadBalancerStats& stats)
    : RingHashLoadBalancer::Ring(normalized_host_weights, min_normalized_weight, min_ring_size,
                                 max_ring_size, hash_function, use_hostname_for_hashing,
                                 ring_hash_stats),
      offset_(offset), width_(width), rng_(random_dev_()), random_distribution_(0, 1),
      stats_(stats) {
  unit_width_ = (1.0 / ring_size_);
}

absl::optional<size_t> DeterministicApertureLoadBalancer::Ring::getIndex(double offset) const {
  if (offset < 0 || (offset >= unit_width_ * ring_size_)) {
    return absl::nullopt;
  }
  return offset / unit_width_;
}

absl::optional<double> DeterministicApertureLoadBalancer::Ring::weight(size_t index, double offset,
                                                                       double width) const {
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

double DeterministicApertureLoadBalancer::Ring::intersect(double b0, double e0, double b1,
                                                          double e1) const {
  ENVOY_LOG(trace, "Overlap for (b0: {}, e0: {}, b1: {}, e1: {})", b0, e0, b1, e1);
  return std::max(0.0, std::min(e0, e1) - std::max(b0, b1));
}

absl::optional<size_t> DeterministicApertureLoadBalancer::Ring::pick() const {
  if (width_ > 1.0 || width_ < 0) {
    return absl::nullopt;
  }

  return getIndex(std::fmod((offset_ + width_ * nextRandom()), 1.0));
}

absl::optional<size_t> DeterministicApertureLoadBalancer::Ring::tryPickSecond(size_t first) const {
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

absl::optional<std::pair<size_t, size_t>> DeterministicApertureLoadBalancer::Ring::pick2() const {
  ENVOY_LOG(trace, "pick2 for offset: {}, width: {}", offset_, width_);
  absl::optional<size_t> first = pick();

  if (!first) {
    stats_.pick2_errors_.inc();
    return absl::nullopt;
  }

  absl::optional<size_t> second = tryPickSecond(first.value());
  if (!second) {
    stats_.pick2_errors_.inc();
    return absl::nullopt;
  }

  ENVOY_LOG(trace, "Returning: ({}, {})", *first, *second);
  return std::pair<size_t, size_t>(*first, *second);
}

} // namespace Upstream
} // namespace Envoy
