#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"

namespace Envoy {
namespace Quic {

quic::QuicAlarm* EnvoyQuicAlarmFactory::CreateAlarm(quic::QuicAlarm::Delegate* delegate) {
  return new EnvoyQuicAlarm(scheduler_, clock_,
                            quic::QuicArenaScopedPtr<quic::QuicAlarm::Delegate>(delegate));
}

quic::QuicArenaScopedPtr<quic::QuicAlarm>
EnvoyQuicAlarmFactory::CreateAlarm(quic::QuicArenaScopedPtr<quic::QuicAlarm::Delegate> delegate,
                                   quic::QuicConnectionArena* arena) {
  if (arena != nullptr) {
    return arena->New<EnvoyQuicAlarm>(scheduler_, clock_, std::move(delegate));
  }
  return quic::QuicArenaScopedPtr<quic::QuicAlarm>(
      new EnvoyQuicAlarm(scheduler_, clock_, std::move(delegate)));
}

} // namespace Quic
} // namespace Envoy
