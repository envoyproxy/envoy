#include "test/test_common/network_utility.h"

#include "common/common/assert.h"
#include "common/network/address_impl.h"
#include "test/test_common/utility.h"

namespace Network {
namespace Test {

Address::InstanceConstSharedPtr checkPortAvailability(Address::InstanceConstSharedPtr addr_port,
                                                      Address::SocketType type) {
  if (addr_port == nullptr || addr_port->type() != Address::Type::Ip) {
    ADD_FAILURE() << "Not an internet address: " << (addr_port == nullptr ? "nullptr"
                                                                          : addr_port->asString());
    return nullptr;
  }
  const int fd = addr_port->socket(type);
  if (fd < 0) {
    const int err = errno;
    ADD_FAILURE() << "socket failed for '" << addr_port->asString()
                  << "' with error: " << strerror(err) << " (" << err << ")";
    return nullptr;
  }
  ScopedFdCloser closer(fd);
  // Not setting REUSEADDR, therefore if the address has been recently used we won't reuse it here.
  // However, because we're going to use the address while checking if it is available, we'll need
  // to set REUSEADDR on listener sockets created by tests using an address validated by this means.
  int rc = addr_port->bind(fd);
  const char* failing_fn = nullptr;
  if (rc != 0) {
    failing_fn = "bind";
  } else if (type == Address::SocketType::Stream) {
    // Try listening on the port also, if the type is TCP.
    rc = ::listen(fd, 1);
    if (rc != 0) {
      failing_fn = "listen";
    }
  }
  if (failing_fn != nullptr) {
    const int err = errno;
    if (err == EADDRINUSE) {
      // The port is already in use. Perfectly normal.
      return nullptr;
    } else if (err == EACCES) {
      // A privileged port, and we don't have privileges. Might want to log this.
      return nullptr;
    }
    // Unexpected failure.
    ADD_FAILURE() << failing_fn << " failed for '" << addr_port->asString()
                  << "' with error: " << strerror(err) << " (" << err << ")";
    return nullptr;
  }
  if (addr_port->ip()->port() == 0) {
    // If the port we bind is zero, then the OS will pick a free port for us (assuming there are
    // any), and we need to find out the port number that the OS picked so we can return it.
    sockaddr_storage ss;
    socklen_t ss_len = sizeof ss;
    rc = ::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &ss_len);
    if (rc != 0) {
      const int err = errno;
      ADD_FAILURE() << "getsockname failed for '" << addr_port->asString()
                    << "' with error: " << strerror(err) << " (" << err << ")";
      return nullptr;
    }
    switch (ss.ss_family) {
    case AF_INET: {
      EXPECT_EQ(ss_len, sizeof(sockaddr_in));
      const struct sockaddr_in* sin = reinterpret_cast<const struct sockaddr_in*>(&ss);
      EXPECT_EQ(AF_INET, sin->sin_family);
      addr_port.reset(new Address::Ipv4Instance(sin));
      break;
    }
    case AF_INET6: {
      EXPECT_EQ(ss_len, sizeof(sockaddr_in6));
      const struct sockaddr_in6* sin6 = reinterpret_cast<const struct sockaddr_in6*>(&ss);
      EXPECT_EQ(AF_INET6, sin6->sin6_family);
      addr_port.reset(new Address::Ipv6Instance(*sin6));
      break;
    }
    default:
      ADD_FAILURE() << "Unexpected family in getsockname result: " << ss.ss_family;
      return nullptr;
    }
  }
  return addr_port;
}

Address::InstanceConstSharedPtr checkPortAvailability(const std::string& addr_port,
                                                      Address::SocketType type) {
  auto instance = Address::parseInternetAddressAndPort(addr_port);
  if (instance != nullptr) {
    instance = checkPortAvailability(instance, type);
  } else {
    ADD_FAILURE() << "Unable to parse as an address and port: " << addr_port;
  }
  return instance;
}

} // Test
} // Network
