#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"

namespace Envoy  {
namespace Quic {
EnvoyQuicConnectionHelper::EnvoyQuicConnectionHelper(Event::TimeSystem& time_system) : clock_(time_system), random_generator_(quic::QuicRandom::GetInstance()) {}


} // namespace Quic
} // namespace Envoy
