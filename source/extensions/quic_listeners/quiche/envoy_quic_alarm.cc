#include "extensions/quic_listeners/quiche/envoy_quic_alarm.h"

#include <algorithm>

namespace Envoy {
namespace Quic {

EnvoyQuicAlarm::EnvoyQuicAlarm(Event::Dispatcher& dispatcher, const quic::QuicClock& clock,
                               quic::QuicArenaScopedPtr<quic::QuicAlarm::Delegate> delegate)
    : QuicAlarm(std::move(delegate)), dispatcher_(dispatcher),
      timer_(dispatcher_.createTimer([this]() { Fire(); })), clock_(clock) {}

void EnvoyQuicAlarm::CancelImpl() { timer_->disableTimer(); }

void EnvoyQuicAlarm::SetImpl() {
  quic::QuicTime::Delta duration = getDurationBeforeDeadline();
  // Round up the duration so that any duration < 1us will not be triggered within current event
  // loop. QUICHE alarm is not expected to be scheduled in current event loop. This bit is a bummer
  // in QUICHE, and we are working on the fix. Once QUICHE is fixed of expecting this behavior, we
  // no longer need to round up the duration.
  // TODO(antoniovicente) improve the timer behavior in such case.
  timer_->enableHRTimer(
      std::chrono::microseconds(std::max(static_cast<int64_t>(1), duration.ToMicroseconds())));
}

void EnvoyQuicAlarm::UpdateImpl() {
  // Since Timer::enableTimer() overrides its deadline from previous calls,
  // there is no need to disable the timer before enabling it again.
  SetImpl();
}

quic::QuicTime::Delta EnvoyQuicAlarm::getDurationBeforeDeadline() {
  quic::QuicTime::Delta duration(quic::QuicTime::Delta::Zero());
  quic::QuicTime now = clock_.ApproximateNow();
  quic::QuicTime tmp = deadline();
  if (tmp > now) {
    duration = tmp - now;
  }
  return duration;
}

} // namespace Quic
} // namespace Envoy
