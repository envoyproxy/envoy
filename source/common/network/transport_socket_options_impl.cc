#include "common/network/transport_socket_options_impl.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Network {
void TransportSocketOptionsImpl::hashKey(std::vector<uint8_t>& key) const {
  if (!override_server_name_.has_value()) {
    return;
  }

  uint64_t hash = StringUtil::CaseInsensitiveHash()(override_server_name_.value());

  uint8_t* byte_ptr = reinterpret_cast<uint8_t*>(&hash);
  for (int byte_index = 0; byte_index < 8; byte_index++) {
    key.push_back(*byte_ptr);
    byte_ptr++;
  }
}
} // namespace Network
} // namespace Envoy
