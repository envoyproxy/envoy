#pragma once

#include "envoy/api/os_sys_calls.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/listen_socket.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Network {

class SocketOptionImpl : public Socket::Option, Logger::Loggable<Logger::Id::connection> {
public:
  SocketOptionImpl(envoy::config::core::v3::SocketOption::SocketState in_state,
                   Network::SocketOptionName optname,
                   int value, // Yup, int. See setsockopt(2).
                   absl::optional<Network::Socket::Type> socket_type = absl::nullopt)
      : SocketOptionImpl(in_state, optname,
                         absl::string_view(reinterpret_cast<char*>(&value), sizeof(value)),
                         socket_type) {}

  SocketOptionImpl(envoy::config::core::v3::SocketOption::SocketState in_state,
                   Network::SocketOptionName optname, absl::string_view value,
                   absl::optional<Network::Socket::Type> socket_type = absl::nullopt)
      : in_state_(in_state), optname_(optname), value_(value.begin(), value.end()),
        socket_type_(socket_type) {
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
  const envoy::config::core::v3::SocketOption::SocketState in_state_;
  const Network::SocketOptionName optname_;
  // This has to be a std::vector<uint8_t> but not std::string because std::string might inline
  // the buffer so its data() is not aligned in to alignof(void*).
  const std::vector<uint8_t> value_;
  // If present, specifies the socket type that this option applies to. Attempting to set this
  // option on a socket of a different type will be a no-op.
  absl::optional<Network::Socket::Type> socket_type_;
};

} // namespace Network
} // namespace Envoy
