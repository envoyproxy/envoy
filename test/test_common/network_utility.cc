#include "test/test_common/network_utility.h"

#include <netinet/ip.h>
#include <sys/socket.h>

#include <cstdint>
#include <string>

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/network/address_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Network {
namespace Test {

Address::InstanceConstSharedPtr findOrCheckFreePort(Address::InstanceConstSharedPtr addr_port,
                                                    Address::SocketType type) {
  if (addr_port == nullptr || addr_port->type() != Address::Type::Ip) {
    ADD_FAILURE() << "Not an internet address: "
                  << (addr_port == nullptr ? "nullptr" : addr_port->asString());
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
  // If the port we bind is zero, then the OS will pick a free port for us (assuming there are
  // any), and we need to find out the port number that the OS picked so we can return it.
  if (addr_port->ip()->port() == 0) {
    return Address::addressFromFd(fd);
  }
  return addr_port;
}

Address::InstanceConstSharedPtr findOrCheckFreePort(const std::string& addr_port,
                                                    Address::SocketType type) {
  auto instance = Utility::parseInternetAddressAndPort(addr_port);
  if (instance != nullptr) {
    instance = findOrCheckFreePort(instance, type);
  } else {
    ADD_FAILURE() << "Unable to parse as an address and port: " << addr_port;
  }
  return instance;
}

const std::string getLoopbackAddressUrlString(const Address::IpVersion version) {
  if (version == Address::IpVersion::v6) {
    return std::string("[::1]");
  }
  return std::string("127.0.0.1");
}

const std::string getLoopbackAddressString(const Address::IpVersion version) {
  if (version == Address::IpVersion::v6) {
    return std::string("::1");
  }
  return std::string("127.0.0.1");
}

const std::string getAnyAddressUrlString(const Address::IpVersion version) {
  if (version == Address::IpVersion::v6) {
    return std::string("[::]");
  }
  return std::string("0.0.0.0");
}

const std::string addressVersionAsString(const Address::IpVersion version) {
  if (version == Address::IpVersion::v4) {
    return std::string("v4");
  }
  return std::string("v6");
}

Address::InstanceConstSharedPtr getCanonicalLoopbackAddress(Address::IpVersion version) {
  if (version == Address::IpVersion::v4) {
    return Network::Utility::getCanonicalIpv4LoopbackAddress();
  }
  return Network::Utility::getIpv6LoopbackAddress();
}

Address::InstanceConstSharedPtr getAnyAddress(const Address::IpVersion version) {
  if (version == Address::IpVersion::v4) {
    return Network::Utility::getIpv4AnyAddress();
  }
  return Network::Utility::getIpv6AnyAddress();
}

bool supportsIpVersion(const Address::IpVersion version) {
  Address::InstanceConstSharedPtr addr = getCanonicalLoopbackAddress(version);
  const int fd = addr->socket(Address::SocketType::Stream);
  if (fd < 0) {
    // Socket creation failed.
    return false;
  }
  if (0 != addr->bind(fd)) {
    // Socket bind failed.
    RELEASE_ASSERT(::close(fd) == 0);
    return false;
  }
  RELEASE_ASSERT(::close(fd) == 0);
  return true;
}

std::pair<Address::InstanceConstSharedPtr, int> bindFreeLoopbackPort(Address::IpVersion version,
                                                                     Address::SocketType type) {
  Address::InstanceConstSharedPtr addr = getCanonicalLoopbackAddress(version);
  const char* failing_fn = nullptr;
  const int fd = addr->socket(type);
  if (fd < 0) {
    failing_fn = "socket";
  } else if (0 != addr->bind(fd)) {
    failing_fn = "bind";
  } else {
    return std::make_pair(Address::addressFromFd(fd), fd);
  }
  const int err = errno;
  if (fd >= 0) {
    close(fd);
  }
  std::string msg = fmt::format("{} failed for address {} with error: {} ({})", failing_fn,
                                addr->asString(), strerror(err), err);
  ADD_FAILURE() << msg;
  throw EnvoyException(msg);
}

TransportSocketPtr createRawBufferSocket() { return std::make_unique<RawBufferSocket>(); }

TransportSocketFactoryPtr createRawBufferSocketFactory() {
  return std::make_unique<RawBufferSocketFactory>();
};
} // namespace Test
} // namespace Network
} // namespace Envoy
