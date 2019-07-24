#include "extensions/quic_listeners/quiche/envoy_quic_alarm.h"

namespace Envoy {
namespace Quic {

EnvoyQuicAlarm::EnvoyQuicAlarm(Event::Dispatcher& dispatcher, const quic::QuicClock& clock,
                               quic::QuicArenaScopedPtr<quic::QuicAlarm::Delegate> delegate)
    : QuicAlarm(std::move(delegate)), dispatcher_(dispatcher),
      timer_(dispatcher_.createTimer([this]() { Fire(); })), clock_(clock) {}

void EnvoyQuicAlarm::CancelImpl() { timer_->disableTimer(); }

void EnvoyQuicAlarm::SetImpl() {
  // TODO(#7170) switch to use microseconds if it is supported.
  timer_->enableTimer(std::chrono::milliseconds(getDurationBeforeDeadline().ToMilliseconds()));
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
