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
  quic::QuicTime::Delta duration = getDurationBeforeDeadline();
  // Round up the duration so that any duration < 1ms other than 0 will not be
  // considered as within current event loop.
  // QUICHE alarm has finer granularity than 1ms, and it is not expected to be
  // scheduled in current event loop. This bit is a bummer in QUICHE, and we are
  // working on the fix. Once upstream is fixed, we no longer need to round up
  // the duration.
  timer_->enableTimer(std::chrono::milliseconds(
      (duration + quic::QuicTime::Delta::FromMicroseconds(999)).ToMilliseconds()));
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
