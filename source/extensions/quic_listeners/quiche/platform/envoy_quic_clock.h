#pragma once

#include <chrono>

#include "envoy/event/dispatcher.h"

#include "quiche/quic/platform/api/quic_clock.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicClock : public quic::QuicClock {
public:
  EnvoyQuicClock(Event::Dispatcher& dispatcher);
  ~EnvoyQuicClock() override;

  // quic::QuicClock
  quic::QuicTime ApproximateNow() const override;
  quic::QuicTime Now() const override;
  quic::QuicWallTime WallNow() const override;

private:
  template <typename T> int64_t microsecondsSinceEpoch(std::chrono::time_point<T> time) const {
    return std::chrono::duration_cast<std::chrono::microseconds>(time.time_since_epoch()).count();
  }

  void updateApproximateNow() const;
  quic::QuicTime nowImpl() const;

  Event::Dispatcher& dispatcher_;
  mutable quic::QuicTime approximate_now_;
};

} // namespace Quic
} // namespace Envoy
