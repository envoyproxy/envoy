#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "absl/base/internal/spinlock.h"
#include "absl/base/thread_annotations.h"

namespace Envoy {

// Encapsulates a contention hook which can be registered with absl::RegisterMutexTracer() which
// records statistics about that contention. Should generally be accessed through ::GetTracer(),
// which delivers a global singleton of the MutexTracer. This global singleton is necessary because
// of the type signature which absl::RegisterMutexTracer() expects.
//
// This class is thread-safe, and can be called from multiple mutexes in contention across multiple
// threads. This is made possible by utilizing memory_order_relaxed atomic writes.
class MutexTracer final {
public:
  // Returns the global singleton MutexTracer.
  static MutexTracer* GetTracer();

  // Returns the callback which can be registered via
  // absl::RegsiterMutexTracer(&Envoy::MutexTracer::ContentionHook).
  static void ContentionHook(const char* msg, const void* obj, int64_t wait_cycles);

  // Resets the recorded statistics.
  void Reset();

  int64_t GetNumContentions() { return num_contentions_.load(order_); }
  int64_t GetCurrentWaitCycles() { return current_wait_cycles_.load(order_); }
  int64_t GetLifetimeWaitCycles() { return lifetime_wait_cycles_.load(order_); }

private:
  // Utility function for ContentionHook.
  void RecordContention(const char*, const void*, int64_t wait_cycles);

  // Number of mutex contention occurrences since last reset.
  std::atomic<int64_t> num_contentions_;
  // Length of the current contention wait cycle.
  std::atomic<int64_t> current_wait_cycles_;
  // Total sum of all wait cycles.
  std::atomic<int64_t> lifetime_wait_cycles_;

  // We utilize std::memory_order_relaxed for all operations for the least possible contention.
  std::memory_order order_{std::memory_order_relaxed};
};

} // namespace Envoy
