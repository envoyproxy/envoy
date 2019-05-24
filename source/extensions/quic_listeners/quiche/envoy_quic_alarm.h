#pragma once

#include "envoy/event/timer.h"

#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_clock.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicAlarm : public quic::QuicAlarm {
public:
  EnvoyQuicAlarm(Event::Scheduler& scheduler, quic::QuicClock& clock,
                 quic::QuicArenaScopedPtr<quic::QuicAlarm::Delegate> delegate);

  ~EnvoyQuicAlarm() override{};

  // quic::QuicAlarm
  void CancelImpl() override;
  void SetImpl() override;
  // Overridden to avoid cancel before set.
  void UpdateImpl() override;

private:
  quic::QuicTime::Delta getDurationBeforeDeadline();

  Event::Scheduler& scheduler_;
  Event::TimerPtr timer_{nullptr};
  quic::QuicClock& clock_;
};

} // namespace Quic
} // namespace Envoy
