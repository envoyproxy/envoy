#include "extensions/quic_listeners/quiche/platform/envoy_quic_clock.h"

namespace Envoy {
namespace Quic {

quic::QuicTime EnvoyQuicClock::ApproximateNow() const {
  return quic::QuicTime::Zero() + quic::QuicTime::Delta::FromMicroseconds(microsecondsSinceEpoch(
                                      dispatcher_.approximateMonotonicTime()));
}

quic::QuicTime EnvoyQuicClock::Now() const {
  // Since the expensive operation of obtaining time has to be performed anyway,
  // make Dispatcher update approximate time. Without this, alarms might fire
  // one event loop later. const_cast is necessary here because
  // updateApproximateMonotonicTime() is a non-const operation, and Now() is
  // conceptually const (even though this particular implementation has a
  // visible side effect). Changing Now() to non-const would necessitate
  // changing a number of other methods and members to non-const, which would
  // not increase clarity.
  const_cast<Event::Dispatcher&>(dispatcher_).updateApproximateMonotonicTime();
  return ApproximateNow();
}

quic::QuicWallTime EnvoyQuicClock::WallNow() const {
  return quic::QuicWallTime::FromUNIXMicroseconds(
      microsecondsSinceEpoch(dispatcher_.timeSource().systemTime()));
}

} // namespace Quic
} // namespace Envoy
