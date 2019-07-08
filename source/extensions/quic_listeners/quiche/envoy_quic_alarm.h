#pragma once

#include "envoy/event/timer.h"

#include "common/common/assert.h"

#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_clock.h"

namespace Envoy {
namespace Quic {

// Implements QUIC interface
// https://quiche.googlesource.com/quiche/+/refs/heads/master/quic/core/quic_alarm.h This class
// wraps an Event::Timer object and provide interface for QUIC to interact with the timer.
class EnvoyQuicAlarm : public quic::QuicAlarm {
public:
  EnvoyQuicAlarm(Event::Scheduler& scheduler, quic::QuicClock& clock,
                 quic::QuicArenaScopedPtr<quic::QuicAlarm::Delegate> delegate);

  ~EnvoyQuicAlarm() override { ASSERT(!IsSet()); };

  // quic::QuicAlarm
  void CancelImpl() override;
  void SetImpl() override;
  // Overridden to avoid cancel before set.
  void UpdateImpl() override;

private:
  quic::QuicTime::Delta getDurationBeforeDeadline();

  Event::Scheduler& scheduler_;
  Event::TimerPtr timer_;
  quic::QuicClock& clock_;
};

} // namespace Quic
} // namespace Envoy
