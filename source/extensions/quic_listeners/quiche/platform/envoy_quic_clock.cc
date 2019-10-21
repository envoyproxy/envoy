#include "extensions/quic_listeners/quiche/platform/envoy_quic_clock.h"

namespace Envoy {
namespace Quic {

EnvoyQuicClock::EnvoyQuicClock(Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher), approximate_now_(nowImpl()) {
  dispatcher_.registerOnPrepareCallback(std::bind(&EnvoyQuicClock::updateApproximateNow, this));
}

EnvoyQuicClock::~EnvoyQuicClock() {
  // Unregister callback.
  dispatcher_.registerOnPrepareCallback({});
}

quic::QuicTime EnvoyQuicClock::ApproximateNow() const { return approximate_now_; }

quic::QuicTime EnvoyQuicClock::Now() const {
  approximate_now_ = nowImpl();
  return approximate_now_;
}

quic::QuicWallTime EnvoyQuicClock::WallNow() const {
  return quic::QuicWallTime::FromUNIXMicroseconds(
      microsecondsSinceEpoch(dispatcher_.timeSource().systemTime()));
}

void EnvoyQuicClock::updateApproximateNow() const { approximate_now_ = nowImpl(); }

quic::QuicTime EnvoyQuicClock::nowImpl() const {
  return quic::QuicTime::Zero() + quic::QuicTime::Delta::FromMicroseconds(microsecondsSinceEpoch(
                                      dispatcher_.timeSource().monotonicTime()));
}

} // namespace Quic
} // namespace Envoy
