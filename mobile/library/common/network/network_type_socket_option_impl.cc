#include "library/common/network/network_type_socket_option_impl.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/socket.h"

#include "source/common/common/scalar_to_byte_vector.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Network {

NetworkTypeSocketOptionImpl::NetworkTypeSocketOptionImpl(int network_type)
    : optname_(0, 0, "network_type"), network_type_(network_type) {}

bool NetworkTypeSocketOptionImpl::setOption(
    Socket& /*socket*/, envoy::config::core::v3::SocketOption::SocketState /*state*/) const {
  return true;
}

void NetworkTypeSocketOptionImpl::hashKey(std::vector<uint8_t>& hash_key) const {
  pushScalarToByteVector(network_type_, hash_key);
}

absl::optional<Socket::Option::Details> NetworkTypeSocketOptionImpl::getOptionDetails(
    const Socket&, envoy::config::core::v3::SocketOption::SocketState /*state*/) const {
  Socket::Option::Details details;
  details.name_ = optname_;
  std::vector<uint8_t> data;
  pushScalarToByteVector(network_type_, data);
  details.value_ = std::string(reinterpret_cast<char*>(data.data()), data.size());
  return absl::make_optional(std::move(details));
}

bool NetworkTypeSocketOptionImpl::isSupported() const { return true; }

} // namespace Network
} // namespace Envoy
