#include "common/tcp/conn_info.h"

namespace Envoy {
namespace Tcp {

absl::optional<std::chrono::milliseconds>
ConnectionInfo::lastRoundTripTime(Envoy::Network::Socket* socket) {
  return {};
}

} // namespace Tcp
} // namespace Envoy
