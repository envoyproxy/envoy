#pragma once

#include "envoy/common/platform.h"
#include "envoy/common/pure.h"
#include "envoy/network/socket.h"

namespace Envoy {
namespace Network {
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
   * @return @ref Network::IoHandlePtr that wraps the underlying socket file descriptor
   */
  virtual IoHandlePtr socket(Socket::Type type, Address::Type addr_type, Address::IpVersion version,
                             bool socket_v6only) const PURE;

  /**
   * Low level api to create a socket in the underlying host stack. Does not create an
   * @ref Network::SocketImpl
   * @param socket_type type of socket requested
   * @param addr address that is gleaned for address type and version if needed
   * @return @ref Network::IoHandlePtr that wraps the underlying socket file descriptor
   */
  virtual IoHandlePtr socket(Socket::Type socket_type,
                             const Address::InstanceConstSharedPtr addr) const PURE;

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
                                          const Address::InstanceConstSharedPtr addr) {
  return addr->socketInterface().socket(type, addr);
}

} // namespace Network
} // namespace Envoy
