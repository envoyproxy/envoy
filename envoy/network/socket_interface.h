#pragma once

#include <stddef.h>

#include "envoy/common/platform.h"
#include "envoy/common/pure.h"
#include "envoy/network/socket.h"

namespace Envoy {
namespace Network {

/**
 * Options for creating a socket.
 */
struct SocketCreationOptions {
  // Specifies whether MPTCP should be enabled on the socket. This is only valid for Stream sockets,
  // and only valid on Linux.
  bool mptcp_enabled_{false};

  // Specifies the maximum size of the cache of the address instances associated with
  // packets received by this socket.
  // If this is 0, no addresses will be cached.
  // Is only valid for datagram sockets.
  size_t max_addresses_cache_size_{0};

  bool operator==(const SocketCreationOptions& rhs) const {
    return mptcp_enabled_ == rhs.mptcp_enabled_ &&
           max_addresses_cache_size_ == rhs.max_addresses_cache_size_;
  }
};

class SocketInterface {
public:
  virtual ~SocketInterface() = default;

  /**
   * Low level api to create a socket in the underlying host stack. Does not create a
   * @ref Network::SocketImpl
   * @param type type of socket requested
   * @param addr_type type of address used with the socket
   * @param version IP version if address type is IP
   * @param socket_v6only if the socket is ipv6 version only
   * @param options additional options for how to create the socket
   * @return @ref Network::IoHandlePtr that wraps the underlying socket file descriptor
   */
  virtual IoHandlePtr socket(Socket::Type type, Address::Type addr_type, Address::IpVersion version,
                             bool socket_v6only, const SocketCreationOptions& options) const PURE;

  /**
   * Low level api to create a socket in the underlying host stack. Does not create an
   * @ref Network::SocketImpl
   * @param socket_type type of socket requested
   * @param addr address that is gleaned for address type and version if needed
   * @param options additional options for how to create the socket
   * @return @ref Network::IoHandlePtr that wraps the underlying socket file descriptor
   */
  virtual IoHandlePtr socket(Socket::Type socket_type, const Address::InstanceConstSharedPtr addr,
                             const SocketCreationOptions& options) const PURE;

  /**
   * Returns true if the given family is supported on this machine.
   * @param domain the IP family.
   */
  virtual bool ipFamilySupported(int domain) PURE;
};

using SocketInterfacePtr = std::unique_ptr<SocketInterface>;

/**
 * Create IoHandle for given address.
 * @param type type of socket to be requested
 * @param addr address that is gleaned for address type, version and socket interface name
 * @return @ref Network::IoHandlePtr that wraps the underlying socket file descriptor
 */
static inline IoHandlePtr ioHandleForAddr(Socket::Type type,
                                          const Address::InstanceConstSharedPtr addr,
                                          const SocketCreationOptions& options) {
  return addr->socketInterface().socket(type, addr, options);
}

} // namespace Network
} // namespace Envoy
