#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "quiche/epoll_server/simple_epoll_server.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_time.h"

namespace quic {

// Clock to efficiently retrieve an approximately accurate time from an
// epoll_server::SimpleEpollServer.
class QuicEpollClock : public QuicClock {
public:
  explicit QuicEpollClock(epoll_server::SimpleEpollServer* epoll_server);

  QuicEpollClock(const QuicEpollClock&) = delete;
  QuicEpollClock& operator=(const QuicEpollClock&) = delete;

  ~QuicEpollClock() override = default;

  // Returns the approximate current time as a QuicTime object.
  QuicTime ApproximateNow() const override;

  // Returns the current time as a QuicTime object.
  // Note: this uses significant resources, please use only if needed.
  QuicTime Now() const override;

  // Returns the current time as a QuicWallTime object.
  // Note: this uses significant resources, please use only if needed.
  QuicWallTime WallNow() const override;

  // Override to do less work in this implementation. The epoll clock is
  // already based on system (unix epoch) time, no conversion required.
  QuicTime ConvertWallTimeToQuicTime(const QuicWallTime& walltime) const override;

protected:
  epoll_server::SimpleEpollServer* epoll_server_;
  // Largest time returned from Now() so far.
  mutable QuicTime largest_time_;
};

} // namespace quic
