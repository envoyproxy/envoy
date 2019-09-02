#pragma once

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "envoy/api/os_sys_calls.h"
#include "envoy/network/listen_socket.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Network {

#ifdef IP_TRANSPARENT
#define ENVOY_SOCKET_IP_TRANSPARENT ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_IP, IP_TRANSPARENT)
#else
#define ENVOY_SOCKET_IP_TRANSPARENT Network::SocketOptionName()
#endif

#ifdef IPV6_TRANSPARENT
#define ENVOY_SOCKET_IPV6_TRANSPARENT ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_IPV6, IPV6_TRANSPARENT)
#else
#define ENVOY_SOCKET_IPV6_TRANSPARENT Network::SocketOptionName()
#endif

#ifdef IP_FREEBIND
#define ENVOY_SOCKET_IP_FREEBIND ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_IP, IP_FREEBIND)
#else
#define ENVOY_SOCKET_IP_FREEBIND Network::SocketOptionName()
#endif

#ifdef IPV6_FREEBIND
#define ENVOY_SOCKET_IPV6_FREEBIND ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_IPV6, IPV6_FREEBIND)
#else
#define ENVOY_SOCKET_IPV6_FREEBIND Network::SocketOptionName()
#endif

#ifdef SO_KEEPALIVE
#define ENVOY_SOCKET_SO_KEEPALIVE ENVOY_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_KEEPALIVE)
#else
#define ENVOY_SOCKET_SO_KEEPALIVE Network::SocketOptionName()
#endif

#ifdef SO_MARK
#define ENVOY_SOCKET_SO_MARK ENVOY_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_MARK)
#else
#define ENVOY_SOCKET_SO_MARK Network::SocketOptionName()
#endif

#ifdef TCP_KEEPCNT
#define ENVOY_SOCKET_TCP_KEEPCNT ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_TCP, TCP_KEEPCNT)
#else
#define ENVOY_SOCKET_TCP_KEEPCNT Network::SocketOptionName()
#endif

#ifdef TCP_KEEPIDLE
#define ENVOY_SOCKET_TCP_KEEPIDLE ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_TCP, TCP_KEEPIDLE)
#elif TCP_KEEPALIVE // macOS uses a different name from Linux for just this option.
#define ENVOY_SOCKET_TCP_KEEPIDLE ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_TCP, TCP_KEEPALIVE)
#else
#define ENVOY_SOCKET_TCP_KEEPIDLE Network::SocketOptionName()
#endif

#ifdef TCP_KEEPINTVL
#define ENVOY_SOCKET_TCP_KEEPINTVL ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_TCP, TCP_KEEPINTVL)
#else
#define ENVOY_SOCKET_TCP_KEEPINTVL Network::SocketOptionName()
#endif

#ifdef TCP_FASTOPEN
#define ENVOY_SOCKET_TCP_FASTOPEN ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_TCP, TCP_FASTOPEN)
#else
#define ENVOY_SOCKET_TCP_FASTOPEN Network::SocketOptionName()
#endif

// Linux uses IP_PKTINFO for both sending source address and receiving destination
// address.
// FreeBSD uses IP_RECVDSTADDR for receiving destination address and IP_SENDSRCADDR for sending
// source address. And these two have same value for convenience purpose.
#ifdef IP_RECVDSTADDR
#ifdef IP_SENDSRCADDR
static_assert(IP_RECVDSTADDR == IP_SENDSRCADDR);
#endif
#define ENVOY_IP_PKTINFO IP_RECVDSTADDR
#elif IP_PKTINFO
#define ENVOY_IP_PKTINFO IP_PKTINFO
#endif

#define ENVOY_SELF_IP_ADDR ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_IP, ENVOY_IP_PKTINFO)

// Both Linux and FreeBSD use IPV6_RECVPKTINFO for both sending source address and
// receiving destination address.
#define ENVOY_SELF_IPV6_ADDR ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_IPV6, IPV6_RECVPKTINFO)

class SocketOptionImpl : public Socket::Option, Logger::Loggable<Logger::Id::connection> {
public:
  SocketOptionImpl(envoy::api::v2::core::SocketOption::SocketState in_state,
                   Network::SocketOptionName optname,
                   int value) // Yup, int. See setsockopt(2).
      : SocketOptionImpl(in_state, optname,
                         absl::string_view(reinterpret_cast<char*>(&value), sizeof(value))) {}

  SocketOptionImpl(envoy::api::v2::core::SocketOption::SocketState in_state,
                   Network::SocketOptionName optname, absl::string_view value)
      : in_state_(in_state), optname_(optname), value_(value.begin(), value.end()) {
    ASSERT(reinterpret_cast<uintptr_t>(value_.data()) % alignof(void*) == 0);
  }

  // Socket::Option
  bool setOption(Socket& socket,
                 envoy::api::v2::core::SocketOption::SocketState state) const override;

  // The common socket options don't require a hash key.
  void hashKey(std::vector<uint8_t>&) const override {}

  absl::optional<Details>
  getOptionDetails(const Socket& socket,
                   envoy::api::v2::core::SocketOption::SocketState state) const override;

  bool isSupported() const;

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
  const envoy::api::v2::core::SocketOption::SocketState in_state_;
  const Network::SocketOptionName optname_;
  // This has to be a std::vector<uint8_t> but not std::string because std::string might inline
  // the buffer so its data() is not aligned in to alignof(void*).
  const std::vector<uint8_t> value_;
};

} // namespace Network
} // namespace Envoy
