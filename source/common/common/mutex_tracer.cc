#include "common/common/mutex_tracer.h"

#include <memory>

namespace Envoy {

MutexTracer* MutexTracer::GetTracer() {
  static MutexTracer global_tracer;
  return &global_tracer;
}

void MutexTracer::ContentionHook(const char* msg, const void* obj, int64_t wait_cycles) {
  MutexTracer* tracer = MutexTracer::GetTracer();
  tracer->RecordContention(msg, obj, wait_cycles);
}

void MutexTracer::Reset() {
  lock_.Lock();
  data_.reset();
  lock_.Unlock();
}

MutexData MutexTracer::GetData() {
  lock_.Lock();
  MutexData data = data_;
  lock_.Unlock();
  return data;
}

void MutexTracer::RecordContention(const char*, const void*, int64_t wait_cycles) {
  // The mutex contention hook cannot be blocking. We use TryLock() and prefer to discard failing
  // writes than to block on contention stats recording.
  if (!lock_.TryLock()) {
    return;
  }
  data_.flushWaitCycles(wait_cycles);
  lock_.Unlock();
}

} // namespace Envoy
