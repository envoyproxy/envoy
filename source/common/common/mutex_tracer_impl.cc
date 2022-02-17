#include "source/common/common/mutex_tracer_impl.h"

#include <iostream>
#include <memory>

#include "source/common/common/assert.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {

MutexTracerImpl* MutexTracerImpl::singleton_ = nullptr;

MutexTracerImpl& MutexTracerImpl::getOrCreateTracer() {
  if (singleton_ == nullptr) {
    singleton_ = new MutexTracerImpl;
    // There's no easy way to unregister a hook. Luckily, this hook is innocuous enough that it
    // seems safe to leave it registered during testing, even though this technically breaks
    // hermeticity.
    absl::RegisterMutexTracer(&Envoy::MutexTracerImpl::contentionHook);
  }
  return *singleton_;
}

void MutexTracerImpl::contentionHook(const char* msg, const void* obj, int64_t wait_cycles) {
  ASSERT(singleton_ != nullptr);
  singleton_->recordContention(msg, obj, wait_cycles);
}

void MutexTracerImpl::reset() {
  num_contentions_.store(0, order_);
  current_wait_cycles_.store(0, order_);
  lifetime_wait_cycles_.store(0, order_);
}

inline void MutexTracerImpl::recordContention(const char*, const void*, int64_t wait_cycles) {
  num_contentions_.fetch_add(1, order_);
  current_wait_cycles_.store(wait_cycles, order_);
  lifetime_wait_cycles_.fetch_add(wait_cycles, order_);
}

} // namespace Envoy
