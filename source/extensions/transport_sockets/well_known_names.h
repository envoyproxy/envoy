#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {

/**
 * Well-known transport socket names.
 * NOTE: New transport sockets should use the well known name: envoy.transport_sockets.name.
 */
class TransportSocketNameValues {
public:
  const std::string Alts = "envoy.transport_sockets.alts";
  const std::string Tap = "envoy.transport_sockets.tap";
  const std::string RawBuffer = "raw_buffer";
  const std::string Tls = "tls";
  const std::string Quic = "quic";
};

using TransportSocketNames = ConstSingleton<TransportSocketNameValues>;

} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
