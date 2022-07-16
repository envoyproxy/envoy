#include "source/common/network/filter_state_proxy_info.h"

namespace Envoy {
namespace Network {

const std::string& Http11ProxyInfoFilterState::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.transport_socket.http_11_proxy.info");
}

} // namespace Network
} // namespace Envoy
