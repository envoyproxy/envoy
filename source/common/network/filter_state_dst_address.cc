#include "source/common/network/filter_state_dst_address.h"

namespace Envoy {
namespace Network {

const std::string& DestinationAddress::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.transport_socket.original_dst_address");
}

} // namespace Network
} // namespace Envoy
