#include "source/common/network/proxy_receive_before_connect.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

const std::string& ProxyReceiveBeforeConnect::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.proxy_receive_before_connect");
}
} // namespace Network
} // namespace Envoy
