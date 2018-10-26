#include "common/common/mutex_tracer.h"

#include <iostream>
#include <memory>

#include "common/common/assert.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {

MutexTracer* MutexTracer::singleton_ = nullptr;

MutexTracer* MutexTracer::getOrCreateTracer() {
  if (singleton_ == nullptr) {
    singleton_ = new MutexTracer;
    absl::RegisterMutexTracer(&Envoy::MutexTracer::contentionHook);
  }
  return singleton_;
}

void MutexTracer::contentionHook(const char* msg, const void* obj, int64_t wait_cycles) {
  ASSERT(singleton_ != nullptr);
  singleton_->recordContention(msg, obj, wait_cycles);
}

void MutexTracer::reset() {
  num_contentions_.store(0, order_);
  current_wait_cycles_.store(0, order_);
  lifetime_wait_cycles_.store(0, order_);
}

inline void MutexTracer::recordContention(const char*, const void*, int64_t wait_cycles) {
  num_contentions_.fetch_add(1, order_);
  current_wait_cycles_.store(wait_cycles, order_);
  lifetime_wait_cycles_.fetch_add(wait_cycles, order_);
}

} // namespace Envoy
