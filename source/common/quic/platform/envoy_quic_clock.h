#pragma once

#include <chrono>

#include "envoy/event/dispatcher.h"

#include "quiche/quic/core/quic_clock.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicClock : public quic::QuicClock {
public:
  EnvoyQuicClock(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  // quic::QuicClock
  quic::QuicTime ApproximateNow() const override;
  quic::QuicTime Now() const override;
  quic::QuicWallTime WallNow() const override;

private:
  template <typename T> int64_t microsecondsSinceEpoch(std::chrono::time_point<T> time) const {
    return std::chrono::duration_cast<std::chrono::microseconds>(time.time_since_epoch()).count();
  }

  Event::Dispatcher& dispatcher_;
};

} // namespace Quic
} // namespace Envoy
