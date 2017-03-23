#include "network_utility.h"

#include "common/common/assert.h"
#include "common/network/address_impl.h"
#include "utility.h"

//#include <errno.h>

namespace Network {
namespace Test {

Address::InstanceConstSharedPtr checkPortAvailability(Address::InstanceConstSharedPtr addrPort,
                                                      Address::SocketType type) {
  // Note that there isn't any point in passing in a port of zero as we don't provide
  // a means of finding out what port the OS assigned in that case. That could be fixed
  // if desired.
  RELEASE_ASSERT(addrPort != nullptr);
  RELEASE_ASSERT(addrPort->type() == Address::Type::Ip);
  const int fd = addrPort->socket(type);
  RELEASE_ASSERT(fd >= 0);
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
    RELEASE_ASSERT(false);
  }
  if (addrPort->ip()->port() == 0) {
    // Need to find out the port number that the OS picked.
    sockaddr_storage ss;
    socklen_t ss_len = sizeof ss;
    rc = ::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &ss_len);
    RELEASE_ASSERT(rc >= 0);
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
      RELEASE_ASSERT(false);
    }
  }
  return addrPort;
}

Address::InstanceConstSharedPtr checkPortAvailability(const std::string& addrPort,
                                                      Address::SocketType type) {
  auto instance = Address::parseInternetAddressAndPort(addrPort);
  if (instance != nullptr) {
    instance = checkPortAvailability(instance, type);
  }
  return instance;
}

} // Test
} // Network
