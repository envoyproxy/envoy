// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_epoll_clock.h"

// #include "quiche/quic/platform/api/quic_flags.h"
// #include "quiche/quic/platform/api/quic_flag_utils.h"
// TODO(danzh) remove dummy implementation once quic_flags.h and
// quic_flag_utils.h are implemented.
#define GetQuicReloadableFlag(flag) ({ flag; })
#define SetQuicReloadableFlag(flag, value)                                                         \
  do {                                                                                             \
    flag = value;                                                                                  \
  } while (0)
#define QUIC_RELOADABLE_FLAG_COUNT(flag)                                                           \
  do {                                                                                             \
  } while (0)

namespace quic {

bool quic_monotonic_epoll_clock = false;

QuicEpollClock::QuicEpollClock(epoll_server::SimpleEpollServer* epoll_server)
    : epoll_server_(epoll_server), largest_time_(QuicTime::Zero()) {}

QuicTime QuicEpollClock::ApproximateNow() const {
  return CreateTimeFromMicroseconds(epoll_server_->ApproximateNowInUsec());
}

QuicTime QuicEpollClock::Now() const {
  QuicTime now = CreateTimeFromMicroseconds(epoll_server_->NowInUsec());
  if (!GetQuicReloadableFlag(quic_monotonic_epoll_clock)) {
    return now;
  }

  if (now <= largest_time_) {
    if (now < largest_time_) {
      QUIC_RELOADABLE_FLAG_COUNT(quic_monotonic_epoll_clock);
    }
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
