#include "library/common/network/socket_tag_socket_option_impl.h"

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/scalar_to_byte_vector.h"

#include "library/common/jni/android_jni_utility.h"

namespace Envoy {
namespace Network {

SocketTagSocketOptionImpl::SocketTagSocketOptionImpl(uid_t uid, uint32_t traffic_stats_tag)
    : optname_(0, 0, "socket_tag"), uid_(uid), traffic_stats_tag_(traffic_stats_tag) {}

bool SocketTagSocketOptionImpl::setOption(
    Socket& socket, envoy::config::core::v3::SocketOption::SocketState state) const {
  if (state != envoy::config::core::v3::SocketOption::STATE_PREBIND) {
    return true;
  }

  // Because socket tagging happens at the socket level, not at the request level,
  // requests with different socket tags must not use the same socket. As a result
  // different socket tag socket options must end up in different socket pools.
  // This happens because different socket tag socket option generate different
  // hash keys.
  // Further, this only works for sockets which have a raw fd and will be a no-op
  // otherwise.
  int fd = socket.ioHandle().fdDoNotUse();
  tag_socket(fd, uid_, traffic_stats_tag_);
  return true;
}

void SocketTagSocketOptionImpl::hashKey(std::vector<uint8_t>& hash_key) const {
  pushScalarToByteVector(uid_, hash_key);
  pushScalarToByteVector(traffic_stats_tag_, hash_key);
}

absl::optional<Socket::Option::Details> SocketTagSocketOptionImpl::getOptionDetails(
    const Socket&, envoy::config::core::v3::SocketOption::SocketState /*state*/) const {
  if (!isSupported()) {
    return absl::nullopt;
  }

  static std::string name = "socket_tag";
  Socket::Option::Details details;
  details.name_ = optname_;
  std::vector<uint8_t> data;
  pushScalarToByteVector(uid_, data);
  pushScalarToByteVector(traffic_stats_tag_, data);
  details.value_ = std::string(reinterpret_cast<char*>(data.data()), data.size());
  return absl::make_optional(std::move(details));
}

bool SocketTagSocketOptionImpl::isSupported() const { return optname_.hasValue(); }

} // namespace Network
} // namespace Envoy
