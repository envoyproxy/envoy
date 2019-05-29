#include "extensions/quic_listeners/quiche/envoy_quic_alarm.h"

namespace Envoy {
namespace Quic {

EnvoyQuicAlarm::EnvoyQuicAlarm(Event::Scheduler& scheduler, quic::QuicClock& clock,
                               quic::QuicArenaScopedPtr<quic::QuicAlarm::Delegate> delegate)
    : QuicAlarm(std::move(delegate)), scheduler_(scheduler), clock_(clock) {
  timer_ = scheduler_.createTimer([this]() { Fire(); });
}

void EnvoyQuicAlarm::CancelImpl() { timer_->disableTimer(); }

void EnvoyQuicAlarm::SetImpl() {
  timer_->enableTimerInUs(std::chrono::microseconds(getDurationBeforeDeadline().ToMicroseconds()));
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
