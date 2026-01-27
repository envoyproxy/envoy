#pragma once

#include "envoy/api/os_sys_calls.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/listen_socket.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/memory/aligned_allocator.h"

namespace Envoy {
namespace Network {

class SocketOptionImpl : public Socket::Option, Logger::Loggable<Logger::Id::connection> {
public:
  SocketOptionImpl(envoy::config::core::v3::SocketOption::SocketState in_state,
                   Network::SocketOptionName optname,
                   int value, // Yup, int. See setsockopt(2).
                   absl::optional<Network::Socket::Type> socket_type = absl::nullopt,
                   absl::optional<Network::Address::IpVersion> socket_ip_version = absl::nullopt)
      : SocketOptionImpl(in_state, optname,
                         absl::string_view(reinterpret_cast<char*>(&value), sizeof(value)),
                         socket_type, socket_ip_version) {}

  SocketOptionImpl(envoy::config::core::v3::SocketOption::SocketState in_state,
                   Network::SocketOptionName optname, absl::string_view value,
                   absl::optional<Network::Socket::Type> socket_type = absl::nullopt,
                   absl::optional<Network::Address::IpVersion> socket_ip_version = absl::nullopt)
      : in_state_(in_state), optname_(optname), value_(value.begin(), value.end()),
        socket_type_(socket_type), socket_ip_version_(socket_ip_version) {
    ASSERT(reinterpret_cast<uintptr_t>(value_.data()) % alignof(void*) == 0);
  }

  SocketOptionImpl(Network::SocketOptionName optname, absl::string_view value,
                   absl::optional<Network::Socket::Type> socket_type = absl::nullopt,
                   absl::optional<Network::Address::IpVersion> socket_ip_version = absl::nullopt)
      : in_state_(absl::nullopt), optname_(optname), value_(value.begin(), value.end()),
        socket_type_(socket_type), socket_ip_version_(socket_ip_version) {
    ASSERT(reinterpret_cast<uintptr_t>(value_.data()) % alignof(void*) == 0);
  }

  // Socket::Option
  bool setOption(Socket& socket,
                 envoy::config::core::v3::SocketOption::SocketState state) const override;
  void hashKey(std::vector<uint8_t>& hash_key) const override;
  absl::optional<Details>
  getOptionDetails(const Socket& socket,
                   envoy::config::core::v3::SocketOption::SocketState state) const override;
  bool isSupported() const override;

  /**
   * Gets the socket type for this socket option. Empty means, the socket option is not specific to
   * a particular socket type.
   *
   * @return the socket type
   */
  absl::optional<Network::Socket::Type> socketType() const;

  /**
   * Gets the socket IP version for this socket option. Empty means, the socket option is not
   * specific to a particular socket IP version.
   *
   * @return the socket IP version
   */
  absl::optional<Network::Address::IpVersion> socketIpVersion() const;

  /**
   * Set the option on the given socket.
   * @param socket the socket on which to apply the option.
   * @param optname the option name.
   * @param value the option value.
   * @param size the option value size.
   * @return a Api::SysCallIntResult with rc_ = 0 for success and rc = -1 for failure. If the call
   * is successful, errno_ shouldn't be used.
   */
  static Api::SysCallIntResult setSocketOption(Socket& socket,
                                               const Network::SocketOptionName& optname,
                                               const void* value, size_t size);

private:
  // The state this option expects the socket to be in when it is applied. If the state is not set,
  // then this option will be applied in any state.
  absl::optional<const envoy::config::core::v3::SocketOption::SocketState> in_state_;
  const Network::SocketOptionName optname_;
  // The vector's data() is used by the setsockopt syscall, which needs to be int-size-aligned on
  // some platforms, the AlignedAllocator here makes it pointer-size-aligned, which satisfies the
  // requirement, although it can be slightly over-aligned.
  const std::vector<uint8_t, Memory::AlignedAllocator<uint8_t, alignof(void*)>> value_;
  // If present, specifies the socket type that this option applies to. Attempting to set this
  // option on a socket of a different type will be a no-op.
  absl::optional<Network::Socket::Type> socket_type_;
  // If present, specifies the socket IP version that this option applies to. Attempting to set this
  // option on a socket of a different IP version will be a no-op.
  absl::optional<Network::Address::IpVersion> socket_ip_version_;
};

} // namespace Network
} // namespace Envoy
