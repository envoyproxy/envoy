#include "extensions/tracers/xray/reservoir.h"

#include "common/common/lock_guard.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

Reservoir::Reservoir(const Reservoir& reservoir) : time_source_(reservoir.time_source_) {
  traces_per_second_ = reservoir.tracesPerSecond();
  this_second_ = reservoir.thisSecond();
  used_this_second_ = reservoir.usedThisSecond();
}

Reservoir& Reservoir::operator=(const Reservoir& reservoir) {
  traces_per_second_ = reservoir.tracesPerSecond();
  this_second_ = reservoir.thisSecond();
  used_this_second_ = reservoir.usedThisSecond();
  time_source_ = reservoir.time_source_;
  return *this;
}

Reservoir::Reservoir(int traces_per_second, TimeSource& time_source) : time_source_(time_source) {
  traces_per_second_ = traces_per_second;
  used_this_second_ = 0;
  this_second_ = std::chrono::duration_cast<std::chrono::seconds>(
                     time_source.monotonicTime().time_since_epoch())
                     .count();
}

bool Reservoir::take() {
  int now = std::chrono::duration_cast<std::chrono::seconds>(
                time_source_.monotonicTime().time_since_epoch())
                .count();

  Thread::LockGuard lock(m_lock);
  if (now != this_second_) {
    used_this_second_ = 0;
    this_second_ = now;
  }

  if (used_this_second_ >= traces_per_second_) {
    return false;
  }

  used_this_second_++;
  return true;
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
