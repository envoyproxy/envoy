#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {

/**
 * Well-known io socket names.
 * NOTE: New io sockets should use the well known name: envoy.io_socket.name.
 */
class IoSocketNameValues {
public:
  const std::string BufferedIoSocket = "envoy.io_socket.buffered_io_socket";
};

using IoSocketNames = ConstSingleton<IoSocketNameValues>;

/**
 * Well-known io socket names.
 */
class IoSocketShortNameValues {
public:
  const std::string BufferedIoSocket = "buffered_io_socket";
};

using IoSocketShortNameValues = ConstSingleton<IoSocketShortNameValues>;

} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
