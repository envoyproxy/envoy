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
  ScopedFdCloser closer(fd);
  // Not setting REUSEADDR, therefore if the address has been recently used we won't reuse it here.
  // However, because we're going to use the address while checking if it is available, we'll need
  // to set REUSEADDR on listener sockets created by tests using an address validated by this means.
  Api::SysCallResult result = addr_port->bind(fd);
  int err;
  const char* failing_fn = nullptr;
  if (result.rc_ != 0) {
    err = result.errno_;
    failing_fn = "bind";
  } else if (type == Address::SocketType::Stream) {
    // Try listening on the port also, if the type is TCP.
    if (::listen(fd, 1) != 0) {
      err = errno;
      failing_fn = "listen";
    }
  }
  if (failing_fn != nullptr) {
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

namespace {
// There is no portable way to initialize sockaddr_in6 with a static initializer, do it with a
// helper function instead.
sockaddr_in6 sockaddrIn6Any() {
  sockaddr_in6 v6any = {};
  v6any.sin6_family = AF_INET6;
  v6any.sin6_addr = in6addr_any;

  return v6any;
}
} // namespace

Address::InstanceConstSharedPtr getAnyAddress(const Address::IpVersion version, bool v4_compat) {
  if (version == Address::IpVersion::v4) {
    return Network::Utility::getIpv4AnyAddress();
  }
  if (v4_compat) {
    // This will return an IPv6 ANY address ("[::]:0") like the getIpv6AnyAddress() below, but
    // with the internal 'v6only' member set to false. This will allow a socket created from this
    // address to accept IPv4 connections. IPv4 connections received on IPv6 sockets will have
    // Ipv4-mapped IPv6 addresses, which we will then internally interpret as IPv4 addresses so
    // that, for example, access logging will show IPv4 address format for IPv4 connections even
    // if they were received on an IPv6 socket.
    static Address::InstanceConstSharedPtr any(new Address::Ipv6Instance(sockaddrIn6Any(), false));
    return any;
  }
  return Network::Utility::getIpv6AnyAddress();
}

bool supportsIpVersion(const Address::IpVersion version) {
  Address::InstanceConstSharedPtr addr = getCanonicalLoopbackAddress(version);
  const int fd = addr->socket(Address::SocketType::Stream);
  if (0 != addr->bind(fd).rc_) {
    // Socket bind failed.
    RELEASE_ASSERT(::close(fd) == 0, "");
    return false;
  }
  RELEASE_ASSERT(::close(fd) == 0, "");
  return true;
}

std::pair<Address::InstanceConstSharedPtr, int> bindFreeLoopbackPort(Address::IpVersion version,
                                                                     Address::SocketType type) {
  Address::InstanceConstSharedPtr addr = getCanonicalLoopbackAddress(version);
  const int fd = addr->socket(type);
  Api::SysCallResult result = addr->bind(fd);
  if (0 != result.rc_) {
    close(fd);
    std::string msg = fmt::format("bind failed for address {} with error: {} ({})",
                                  addr->asString(), strerror(result.errno_), result.errno_);
    ADD_FAILURE() << msg;
    throw EnvoyException(msg);
  }
  return std::make_pair(Address::addressFromFd(fd), fd);
}

TransportSocketPtr createRawBufferSocket() { return std::make_unique<RawBufferSocket>(); }

TransportSocketFactoryPtr createRawBufferSocketFactory() {
  return std::make_unique<RawBufferSocketFactory>();
}

const Network::FilterChainSharedPtr
createEmptyFilterChain(TransportSocketFactoryPtr&& transport_socket_factory) {
  return std::make_shared<Network::Test::EmptyFilterChain>(std::move(transport_socket_factory));
}

const Network::FilterChainSharedPtr createEmptyFilterChainWithRawBufferSockets() {
  return createEmptyFilterChain(createRawBufferSocketFactory());
}

} // namespace Test
} // namespace Network
} // namespace Envoy
