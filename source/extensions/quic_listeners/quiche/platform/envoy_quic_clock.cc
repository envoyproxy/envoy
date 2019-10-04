#include "extensions/quic_listeners/quiche/platform/envoy_quic_clock.h"

namespace Envoy {
namespace Quic {

quic::QuicTime EnvoyQuicClock::ApproximateNow() const {
  const timeval prepare_time_ = dispatcher_.getSchedulerPrepareTime();
  return quic::QuicTime::Zero() + quic::QuicTime::Delta::FromSeconds(prepare_time_.tv_sec) +
         quic::QuicTime::Delta::FromMicroseconds(prepare_time_.tv_usec);
}

quic::QuicTime EnvoyQuicClock::Now() const {
  return quic::QuicTime::Zero() + quic::QuicTime::Delta::FromMicroseconds(microsecondsSinceEpoch(
                                      dispatcher_.timeSource().monotonicTime()));
}

quic::QuicWallTime EnvoyQuicClock::WallNow() const {
  return quic::QuicWallTime::FromUNIXMicroseconds(
      microsecondsSinceEpoch(dispatcher_.timeSource().systemTime()));
}

} // namespace Quic
} // namespace Envoy
