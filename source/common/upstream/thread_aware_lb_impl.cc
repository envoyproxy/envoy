#include "common/upstream/thread_aware_lb_impl.h"

namespace Envoy {
namespace Upstream {

void ThreadAwareLoadBalancerBase::initialize() {
  // TODO(mattklein123): In the future, once initialized and the initial LB is built, it would be
  // better to use a background thread for computing LB updates. This has the substantial benefit
  // that if the LB computation thread falls behind, host set updates can be trivially collapsed.
  // I will look into doing this in a follow up. Doing everything using a background thread heavily
  // complicated initialization as the load balancer would need its own initialized callback. I
  // think the synchronous/asynchronous split is probably the best option.
  priority_set_.addMemberUpdateCb(
      [this](uint32_t, const HostVector&, const HostVector&) -> void { refresh(); });

  refresh();
}

void ThreadAwareLoadBalancerBase::refresh() {
  auto per_priority_state_vector = std::make_shared<std::vector<PerPriorityStatePtr>>(
      priority_set_.hostSetsPerPriority().size());
  auto per_priority_load = std::make_shared<std::vector<uint32_t>>(per_priority_load_);

  for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
    const uint32_t priority = host_set->priority();
    (*per_priority_state_vector)[priority].reset(new PerPriorityState);
    const auto& per_priority_state = (*per_priority_state_vector)[priority];
    per_priority_state->current_lb_ = createLoadBalancer(*host_set);
    per_priority_state->global_panic_ = isGlobalPanic(*host_set);
  }

  {
    std::unique_lock<std::shared_timed_mutex> lock(factory_->mutex_);
    factory_->per_priority_load_ = per_priority_load;
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

  const uint32_t priority = LoadBalancerBase::choosePriority(h, *per_priority_load_);
  const auto& per_priority_state = (*per_priority_state_)[priority];
  if (per_priority_state->global_panic_) {
    stats_.lb_healthy_panic_.inc();
  }
  return per_priority_state->current_lb_->chooseHost(h);
}

LoadBalancerPtr ThreadAwareLoadBalancerBase::LoadBalancerFactoryImpl::create() {
  auto lb = std::make_unique<LoadBalancerImpl>(stats_, random_);

  // We must protect current_lb_ via a RW lock since it is accessed and written to by multiple
  // threads. All complex processing has already been precalculated however.
  std::shared_lock<std::shared_timed_mutex> lock(mutex_);
  lb->per_priority_load_ = per_priority_load_;
  lb->per_priority_state_ = per_priority_state_;

  return std::move(lb);
}

} // namespace Upstream
} // namespace Envoy
