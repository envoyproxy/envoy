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

void EnvoyQuicAlarm::UpdateImpl() { SetImpl(); }

quic::QuicTime::Delta EnvoyQuicAlarm::getDurationBeforeDeadline() {
  quic::QuicTime::Delta duration(quic::QuicTime::Delta::Zero());
  quic::QuicTime now = clock_.ApproximateNow();
  if (deadline() > now) {
    duration = deadline() - now;
  }
  return duration;
}

} // namespace Quic
} // namespace Envoy
