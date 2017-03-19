#include "network_utility.h"

#include "envoy/common/exception.h"
#include "common/common/assert.h"
#include "common/network/address_impl.h"
#include "utility.h"

//#include <errno.h>

namespace Network {
namespace Test {

Address::InstancePtr checkPortAvailability(Address::InstancePtr addrPort,
                                           Address::SocketType type) {
  // Note that there isn't any point in passing in a port of zero as we don't provide
  // a means of finding out what port the OS assigned in that case. That could be fixed
  // if desired.
  if (addrPort == nullptr) {
    // TODO(jamessynge) Add logging of err since it isn't expected, for now throwing.
    throw EnvoyException("addrPort == nullptr");
  }
  if (addrPort->type() != Address::Type::Ip) {
    // TODO(jamessynge) Add logging of err since it isn't expected, for now throwing.
    throw EnvoyException(fmt::format("Wrong type of Address::Instance: {}", addrPort->asString()));
  }
  const int fd = addrPort->socket(type);
  if (fd < 0) {
    // This occurs  if the family (e.g. IPv6) isn't supported.
    // TODO(jamessynge) Add debug only logging?
    return nullptr;
  }
  ScopedFdCloser closer(fd);
  // Not setting REUSEADDR, therefore if the address has been recently used we won't reuse it here.
  // However, because we're going to use the address while checking if it is available, we'll need
  // to set REUSEADDR on listener sockets created by tests using an address validated by this means.
  int rc = addrPort->bind(fd);
  if (rc == 0 && type == Address::SocketType::Stream) {
    // Try listening on the port also, if the type is TCP.
    rc = ::listen(fd, 1);
  }
  int err = errno;
  if (rc < 0) {
    if (err == EADDRINUSE) {
      // The port is already in use. Perfectly normal.
      return nullptr;
    } else if (err == EACCES) {
      // A privileged port, and we don't have privileges. Might want to log this.
      return nullptr;
    }
    // TODO(jamessynge) Add logging of err since it isn't expected, for now throwing.
    throw EnvoyException(fmt::format("Unable to bind to address '{}': {} ({})",
                                     addrPort->asString(), strerror(err), err));
  }
  if (addrPort->ip()->port() == 0) {
    // Need to find out the port number that the OS picked.
    sockaddr_storage ss;
    socklen_t ss_len = sizeof ss;
    rc = ::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &ss_len);
    if (rc < 0) {
      err = errno;
      throw new EnvoyException(fmt::format("Unable to getsockname for address '{}': {} ({})",
                                           addrPort->asString(), strerror(err), err));
    }
    switch (ss.ss_family) {
    case AF_INET: {
      ASSERT(ss_len == sizeof(sockaddr_in));
      const struct sockaddr_in* sin = reinterpret_cast<const struct sockaddr_in*>(&ss);
      ASSERT(AF_INET == sin->sin_family);
      addrPort.reset(new Address::Ipv4Instance(sin));
      break;
    }
    case AF_INET6: {
      ASSERT(ss_len == sizeof(sockaddr_in6));
      const struct sockaddr_in6* sin6 = reinterpret_cast<const struct sockaddr_in6*>(&ss);
      ASSERT(AF_INET6 == sin6->sin6_family);
      addrPort.reset(new Address::Ipv6Instance(*sin6));
      break;
    }
    default:
      throw new EnvoyException(fmt::format("Unexpected ss_family ({}) after binding to address {}",
                                           static_cast<int>(ss.ss_family), addrPort->asString()));
    }
  }
  return addrPort;
}

} // Test
} // Network
