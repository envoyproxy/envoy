#include "common/network/application_protocol.h"

#include "common/common/macros.h"

namespace Envoy {
namespace Network {

const std::string& ApplicationProtocols::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.application_protocols");
}
} // namespace Network
} // namespace Envoy
