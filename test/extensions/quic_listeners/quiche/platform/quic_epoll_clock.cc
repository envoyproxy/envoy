// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "test/extensions/quic_listeners/quiche/platform/quic_epoll_clock.h"

namespace quic {

QuicEpollClock::QuicEpollClock(epoll_server::SimpleEpollServer* epoll_server)
    : epoll_server_(epoll_server), largest_time_(QuicTime::Zero()) {}

QuicTime QuicEpollClock::ApproximateNow() const {
  return CreateTimeFromMicroseconds(epoll_server_->ApproximateNowInUsec());
}

QuicTime QuicEpollClock::Now() const {
  QuicTime now = CreateTimeFromMicroseconds(epoll_server_->NowInUsec());

  if (now <= largest_time_) {
    // Time not increasing, return |largest_time_|.
    return largest_time_;
  }

  largest_time_ = now;
  return largest_time_;
}

QuicWallTime QuicEpollClock::WallNow() const {
  return QuicWallTime::FromUNIXMicroseconds(epoll_server_->ApproximateNowInUsec());
}

QuicTime QuicEpollClock::ConvertWallTimeToQuicTime(const QuicWallTime& walltime) const {
  return QuicTime::Zero() + QuicTime::Delta::FromMicroseconds(walltime.ToUNIXMicroseconds());
}

} // namespace quic
