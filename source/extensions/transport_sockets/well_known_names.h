#pragma once

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
  const std::string RAW_BUFFER = "raw_buffer";
  const std::string SSL = "ssl";
};

typedef ConstSingleton<TransportSocketNameValues> TransportSocketNames;

} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
