#include "extensions/quic_listeners/quiche/platform/envoy_quic_clock.h"

namespace Envoy {
namespace Quic {

quic::QuicTime EnvoyQuicClock::ApproximateNow() const {
  // This might be expensive as Dispatcher doesn't store approximate time_point.
  return Now();
}

quic::QuicTime EnvoyQuicClock::Now() const {
  return quic::QuicTime::Zero() + quic::QuicTime::Delta::FromMicroseconds(
                                      microsecondsSinceEpoch(time_system_.monotonicTime()));
}

quic::QuicWallTime EnvoyQuicClock::WallNow() const {
  return quic::QuicWallTime::FromUNIXMicroseconds(
      microsecondsSinceEpoch(time_system_.systemTime()));
}

} // namespace Quic
} // namespace Envoy
