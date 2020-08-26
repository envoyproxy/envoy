#pragma once

#include <chrono>

#include "envoy/network/socket.h"

namespace Envoy {
namespace Tcp {

class ConnectionInfo {
public:
  /**
   *  @return absl::optional<std::chrono::milliseconds> An optional of the most recent round-trip
   *  time of the connection. If the platform does not support this, then an empty optional is
   *  returned.
   */
  static absl::optional<std::chrono::milliseconds>
  ConnectionInfo::lastRoundTripTime(Envoy::Network::Socket& socket);
};

} // namespace Tcp
} // namespace Envoy
