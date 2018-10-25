#include "common/common/mutex_tracer.h"

#include <memory>

namespace Envoy {

MutexTracer* MutexTracer::GetTracer() {
  static MutexTracer global_tracer;
  return &global_tracer;
}

void MutexTracer::ContentionHook(const char* msg, const void* obj, int64_t wait_cycles) {
  GetTracer()->RecordContention(msg, obj, wait_cycles);
}

void MutexTracer::Reset() {
  num_contentions_.store(0, order_);
  current_wait_cycles_.store(0, order_);
  lifetime_wait_cycles_.store(0, order_);
}

void MutexTracer::RecordContention(const char*, const void*, int64_t wait_cycles) {
  num_contentions_.fetch_add(1, order_);
  current_wait_cycles_.store(wait_cycles, order_);
  lifetime_wait_cycles_.fetch_add(wait_cycles, order_);
}

} // namespace Envoy
