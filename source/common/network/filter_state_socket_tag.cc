#include "source/common/network/filter_state_socket_tag.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

const std::string& FilterStateSocketTag::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.transport_socket.socket_tag");
}
} // namespace Network
} // namespace Envoy
