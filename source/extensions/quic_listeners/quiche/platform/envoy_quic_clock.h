#pragma once

#include <chrono>

#include "envoy/event/timer.h"

#include "quiche/quic/platform/api/quic_clock.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicClock : public quic::QuicClock {
public:
  EnvoyQuicClock(Event::TimeSystem& time_system) : time_system_(time_system) {}

  // quic::QuicClock
  quic::QuicTime ApproximateNow() const override;
  quic::QuicTime Now() const override;
  quic::QuicWallTime WallNow() const override;

private:
  template <typename T> int64_t microsecondsSinceEpoch(std::chrono::time_point<T> time) const {
    return std::chrono::duration_cast<std::chrono::microseconds>(time.time_since_epoch()).count();
  }

  Event::TimeSystem& time_system_;
};

} // namespace Quic
} // namespace Envoy
