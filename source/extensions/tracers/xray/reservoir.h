#pragma once

#include <chrono>

#include "envoy/common/time.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/thread.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

/**
 * Simple token-bucket algorithm that enables counting samples/traces used per second.
 */
class Reservoir {
public:
  /**
   * Creates a new reservoir that allows up to |traces_per_second| samples.
   */
  explicit Reservoir(uint32_t traces_per_second) : traces_per_second_(traces_per_second) {}

  Reservoir(const Reservoir& other)
      : traces_per_second_(other.traces_per_second_), used_(other.used_),
        time_point_(other.time_point_) {}

  Reservoir& operator=(const Reservoir& other) {
    if (this == &other) {
      return *this;
    }
    traces_per_second_ = other.traces_per_second_;
    used_ = other.used_;
    time_point_ = other.time_point_;
    return *this;
  }

  /**
   * Determines whether all samples have been used up for this particular second.
   * Every second, this reservoir starts over with a full bucket.
   *
   * @param now Used to compare against the last recorded time to determine if it's still within the
   * same second.
   */
  bool take(Envoy::MonotonicTime now) {
    Envoy::Thread::LockGuard lg(sync_);
    const auto diff = now - time_point_;
    if (diff > std::chrono::seconds(1)) {
      used_ = 0;
      time_point_ = now;
    }

    if (used_ >= traces_per_second_) {
      return false;
    }

    ++used_;
    return true;
  }

private:
  uint32_t traces_per_second_;
  uint32_t used_{0};
  Envoy::MonotonicTime time_point_;
  Envoy::Thread::MutexBasicLockable sync_;
};

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
