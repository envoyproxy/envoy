#pragma once

#include <chrono>

#include "envoy/common/time.h"
#include "quiche/quic/platform/api/quic_clock.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicClock : public quic::QuicClock {
public:
  EnvoyQuicClock(TimesSource& time_source) : time_source_(time_source), quic::QuicClock() {}

  // quic::QuicClock
  quic::QuicTime ApproximateNow() const;
  quic::QuicTime Now() const;
  quic::QuicWallTime WallNow() const;

private:
  template <typename T> int64_t timePointToInt64(std::chrono::time_point<T> time) {
    return std::chrono::duration_cast<std::chrono::microseconds>(time.time_since_epoch()).count();
  }

  TimesSource& time_source_;
};

} // namespace Quic
} // namespace Envoy
