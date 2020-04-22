#include "common/upstream/thread_aware_lb_impl.h"

#include <memory>

namespace Envoy {
namespace Upstream {

// TODO(mergeconflict): Adjust locality weights for partial availability, as is done in
//                      HostSetImpl::effectiveLocalityWeight.
namespace {

void normalizeHostWeights(const HostVector& hosts, double normalized_locality_weight,
                          NormalizedHostWeightVector& normalized_host_weights,
                          double& min_normalized_weight, double& max_normalized_weight) {
  // sum should be at most uint32_t max value, so we can validate it by accumulating into unit64_t
  // and making sure there was no overflow
  uint64_t sum = 0;
  for (const auto& host : hosts) {
    sum += host->weight();
    if (sum > std::numeric_limits<uint32_t>::max()) {
      throw EnvoyException(
          fmt::format("The sum of weights of all upstream hosts in a locality exceeds {}",
                      std::numeric_limits<uint32_t>::max()));
    }
  }

  for (const auto& host : hosts) {
    const double weight = host->weight() * normalized_locality_weight / sum;
    normalized_host_weights.push_back({host, weight});
    min_normalized_weight = std::min(min_normalized_weight, weight);
    max_normalized_weight = std::max(max_normalized_weight, weight);
  }
}

void normalizeLocalityWeights(const HostsPerLocality& hosts_per_locality,
                              const LocalityWeights& locality_weights,
                              NormalizedHostWeightVector& normalized_host_weights,
                              double& min_normalized_weight, double& max_normalized_weight) {
  ASSERT(locality_weights.size() == hosts_per_locality.get().size());

  // sum should be at most uint32_t max value, so we can validate it by accumulating into unit64_t
  // and making sure there was no overflow
  uint64_t sum = 0;
  for (const auto weight : locality_weights) {
    sum += weight;
    if (sum > std::numeric_limits<uint32_t>::max()) {
      throw EnvoyException(
          fmt::format("The sum of weights of all localities at the same priority exceeds {}",
                      std::numeric_limits<uint32_t>::max()));
    }
  }

  // Locality weights (unlike host weights) may be 0. If _all_ locality weights were 0, bail out.
  if (sum == 0) {
    return;
  }

  // Compute normalized weights for all hosts in each locality. If a locality was assigned zero
  // weight, all hosts in that locality will be skipped.
  for (LocalityWeights::size_type i = 0; i < locality_weights.size(); ++i) {
    if (locality_weights[i] != 0) {
      const HostVector& hosts = hosts_per_locality.get()[i];
      const double normalized_locality_weight = static_cast<double>(locality_weights[i]) / sum;
      normalizeHostWeights(hosts, normalized_locality_weight, normalized_host_weights,
                           min_normalized_weight, max_normalized_weight);
    }
  }
}

void normalizeWeights(const HostSet& host_set, bool in_panic,
                      NormalizedHostWeightVector& normalized_host_weights,
                      double& min_normalized_weight, double& max_normalized_weight) {
  if (host_set.localityWeights() == nullptr || host_set.localityWeights()->empty()) {
    // If we're not dealing with locality weights, just normalize weights for the flat set of hosts.
    const auto& hosts = in_panic ? host_set.hosts() : host_set.healthyHosts();
    normalizeHostWeights(hosts, 1.0, normalized_host_weights, min_normalized_weight,
                         max_normalized_weight);
  } else {
    // Otherwise, normalize weights across all localities.
    const auto& hosts_per_locality =
        in_panic ? host_set.hostsPerLocality() : host_set.healthyHostsPerLocality();
    normalizeLocalityWeights(hosts_per_locality, *(host_set.localityWeights()),
                             normalized_host_weights, min_normalized_weight, max_normalized_weight);
  }
}

} // namespace

void ThreadAwareLoadBalancerBase::initialize() {
  // TODO(mattklein123): In the future, once initialized and the initial LB is built, it would be
  // better to use a background thread for computing LB updates. This has the substantial benefit
  // that if the LB computation thread falls behind, host set updates can be trivially collapsed.
  // I will look into doing this in a follow up. Doing everything using a background thread heavily
  // complicated initialization as the load balancer would need its own initialized callback. I
  // think the synchronous/asynchronous split is probably the best option.
  priority_set_.addPriorityUpdateCb(
      [this](uint32_t, const HostVector&, const HostVector&) -> void { refresh(); });

  refresh();
}

void ThreadAwareLoadBalancerBase::refresh() {
  auto per_priority_state_vector = std::make_shared<std::vector<PerPriorityStatePtr>>(
      priority_set_.hostSetsPerPriority().size());
  auto healthy_per_priority_load =
      std::make_shared<HealthyLoad>(per_priority_load_.healthy_priority_load_);
  auto degraded_per_priority_load =
      std::make_shared<DegradedLoad>(per_priority_load_.degraded_priority_load_);

  for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
    const uint32_t priority = host_set->priority();
    (*per_priority_state_vector)[priority] = std::make_unique<PerPriorityState>();
    const auto& per_priority_state = (*per_priority_state_vector)[priority];
    // Copy panic flag from LoadBalancerBase. It is calculated when there is a change
    // in hosts set or hosts' health.
    per_priority_state->global_panic_ = per_priority_panic_[priority];

    // Normalize host and locality weights such that the sum of all normalized weights is 1.
    NormalizedHostWeightVector normalized_host_weights;
    double min_normalized_weight = 1.0;
    double max_normalized_weight = 0.0;
    normalizeWeights(*host_set, per_priority_state->global_panic_, normalized_host_weights,
                     min_normalized_weight, max_normalized_weight);
    per_priority_state->current_lb_ =
        createLoadBalancer(normalized_host_weights, min_normalized_weight, max_normalized_weight);
  }

  {
    absl::WriterMutexLock lock(&factory_->mutex_);
    factory_->healthy_per_priority_load_ = healthy_per_priority_load;
    factory_->degraded_per_priority_load_ = degraded_per_priority_load;
    factory_->per_priority_state_ = per_priority_state_vector;
  }
}

HostConstSharedPtr
ThreadAwareLoadBalancerBase::LoadBalancerImpl::chooseHost(LoadBalancerContext* context) {
  // Make sure we correctly return nullptr for any early chooseHost() calls.
  if (per_priority_state_ == nullptr) {
    return nullptr;
  }

  // If there is no hash in the context, just choose a random value (this effectively becomes
  // the random LB but it won't crash if someone configures it this way).
  // computeHashKey() may be computed on demand, so get it only once.
  absl::optional<uint64_t> hash;
  if (context) {
    hash = context->computeHashKey();
  }
  const uint64_t h = hash ? hash.value() : random_.random();

  const uint32_t priority =
      LoadBalancerBase::choosePriority(h, *healthy_per_priority_load_, *degraded_per_priority_load_)
          .first;
  const auto& per_priority_state = (*per_priority_state_)[priority];
  if (per_priority_state->global_panic_) {
    stats_.lb_healthy_panic_.inc();
  }

  HostConstSharedPtr host;
  const uint32_t max_attempts = context ? context->hostSelectionRetryCount() + 1 : 1;
  for (uint32_t i = 0; i < max_attempts; ++i) {
    host = per_priority_state->current_lb_->chooseHost(h, i);

    // If host selection failed or the host is accepted by the filter, return.
    // Otherwise, try again.
    if (!host || !context || !context->shouldSelectAnotherHost(*host)) {
      return host;
    }
  }

  return host;
}

LoadBalancerPtr ThreadAwareLoadBalancerBase::LoadBalancerFactoryImpl::create() {
  auto lb = std::make_unique<LoadBalancerImpl>(stats_, random_);

  // We must protect current_lb_ via a RW lock since it is accessed and written to by multiple
  // threads. All complex processing has already been precalculated however.
  absl::ReaderMutexLock lock(&mutex_);
  lb->healthy_per_priority_load_ = healthy_per_priority_load_;
  lb->degraded_per_priority_load_ = degraded_per_priority_load_;
  lb->per_priority_state_ = per_priority_state_;

  return lb;
}

} // namespace Upstream
} // namespace Envoy
