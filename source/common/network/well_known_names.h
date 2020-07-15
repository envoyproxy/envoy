#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Network {

/**
 * Well-known UDP writer names
 */
class UdpWriterNameValues {
public:
  const std::string DefaultWriter = "udp_default_writer";
  const std::string GsoBatchWriter = "udp_gso_batch_writer";
};

using UdpWriterNames = ConstSingleton<UdpWriterNameValues>;

} // namespace Network
} // namespace Envoy
