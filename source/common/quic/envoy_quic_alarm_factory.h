#pragma once

#include "source/common/common/non_copyable.h"
#include "source/common/quic/envoy_quic_alarm.h"

#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_arena_scoped_ptr.h"
#include "quiche/quic/core/quic_one_block_arena.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicAlarmFactory : public quic::QuicAlarmFactory, NonCopyable {
public:
  EnvoyQuicAlarmFactory(Event::Dispatcher& dispatcher, const quic::QuicClock& clock)
      : dispatcher_(dispatcher), clock_(clock) {}

  ~EnvoyQuicAlarmFactory() override = default;

  // QuicAlarmFactory
  quic::QuicAlarm* CreateAlarm(quic::QuicAlarm::Delegate* delegate) override;
  quic::QuicArenaScopedPtr<quic::QuicAlarm>
  CreateAlarm(quic::QuicArenaScopedPtr<quic::QuicAlarm::Delegate> delegate,
              quic::QuicConnectionArena* arena) override;

private:
  Event::Dispatcher& dispatcher_;
  const quic::QuicClock& clock_;
};

} // namespace Quic
} // namespace Envoy
