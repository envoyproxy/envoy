#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Server {

/**
 * Well-known active UDP listener names.
 */
class UdpListenerNameValues {
public:
  const std::string RawUdp = "raw_udp_listener";
};

using UdpListenerNames = ConstSingleton<UdpListenerNameValues>;

} // namespace Server
} // namespace Envoy
