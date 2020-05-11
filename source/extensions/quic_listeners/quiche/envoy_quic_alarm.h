#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "common/common/assert.h"

#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_time.h"

namespace Envoy {
namespace Quic {

// Implements QUIC interface
// https://quiche.googlesource.com/quiche/+/refs/heads/master/quic/core/quic_alarm.h This class
// wraps an Event::Timer object and provide interface for QUIC to interact with the timer.
class EnvoyQuicAlarm : public quic::QuicAlarm {
public:
  EnvoyQuicAlarm(Event::Dispatcher& dispatcher, const quic::QuicClock& clock,
                 quic::QuicArenaScopedPtr<quic::QuicAlarm::Delegate> delegate);

  // TimerImpl destruction deletes in-flight alarm firing event.
  ~EnvoyQuicAlarm() override = default;

  // quic::QuicAlarm
  void CancelImpl() override;
  void SetImpl() override;
  // Overridden to avoid cancel before set.
  void UpdateImpl() override;

private:
  quic::QuicTime::Delta getDurationBeforeDeadline();

  Event::Dispatcher& dispatcher_;
  Event::TimerPtr timer_;
  const quic::QuicClock& clock_;
};

} // namespace Quic
} // namespace Envoy
