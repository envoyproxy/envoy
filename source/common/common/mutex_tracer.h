#pragma once

#include <cstddef>
#include <cstdint>

#include "absl/base/internal/spinlock.h"
#include "absl/base/thread_annotations.h"

namespace Envoy {

// Struct containing Mutex contention data.
struct MutexData {
  // Number of mutex contention occurrences since last reset.
  int64_t num_contentions;
  // Length of the current contention wait cycle.
  int64_t current_wait_cycles;
  // Total sum of all wait cycles.
  int64_t lifetime_wait_cycles;

  // Utility function for MutexTracer::RecordContention. At the moment, MutexData only records
  // statistics about the wait_cycles. In the future, more complex data structures could record
  // which mutexes are in contention.
  void flushWaitCycles(int64_t wait_cycles) {
    num_contentions += 1;
    current_wait_cycles = wait_cycles;
    lifetime_wait_cycles += wait_cycles;
  }

  void reset() {
    num_contentions = 0;
    current_wait_cycles = 0;
    lifetime_wait_cycles = 0;
  }
};

// Encapsulates a contention hook which can be registered with absl::RegisterMutexTracer() which
// records statistics about that contention. Should generally be accessed through ::GetTracer(),
// which delivers a global singleton of the MutexTracer. This global singleton is necessary because
// of the type signature which absl::RegisterMutexTracer() expects.
//
// This class is thread-safe, and can be called from multiple mutexes in contention across multiple
// threads. This is made possible by utilizing an absl::base_internal::SpinLock internally, which is
// designed for use in code that Mutex depends on.
class MutexTracer final {
public:
  // Returns the global singleton MutexTracer.
  static MutexTracer* GetTracer();

  // Returns the callback which can be registered via
  // absl::RegsiterMutexTracer(&Envoy::MutexTracer::ContentionHook).
  static void ContentionHook(const char* msg, const void* obj, int64_t wait_cycles);

  // Resets the recorded statistics.
  void Reset();

  // Returns the encapsulated MutexData object.
  MutexData GetData();

private:
  // Utility function for ContentionHook.
  void RecordContention(const char*, const void*, int64_t wait_cycles);

  MutexData data_;

  // It is preferable to use SpinLock instead of absl::Mutex inside a function which absl::Mutex
  // itself depends on.
  absl::base_internal::SpinLock lock_;
};

} // namespace Envoy
