#include "common/network/transport_socket_options_impl.h"

namespace Envoy {
namespace Network {
void TransportSocketOptionsImpl::hashKey(std::vector<uint8_t>& key) const {
  if (!override_server_name_.has_value()) {
    return;
  }

  std::hash<std::string> hash_function;
  key.push_back(hash_function(override_server_name_.value()));
}
} // namespace Network
} // namespace Envoy
