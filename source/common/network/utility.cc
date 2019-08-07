#include "common/network/utility.h"

#include <arpa/inet.h>
#include <ifaddrs.h>

#if defined(__linux__)
#include <linux/netfilter_ipv4.h>
#endif

#ifndef IP6T_SO_ORIGINAL_DST
// From linux/netfilter_ipv6/ip6_tables.h
#define IP6T_SO_ORIGINAL_DST 80
#endif

#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdint>
#include <list>
#include <sstream>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/network/connection.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/cleanup.h"
#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/protobuf/protobuf.h"

#include "common/common/fmt.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Network {

const std::string Utility::TCP_SCHEME = "tcp://";
const std::string Utility::UDP_SCHEME = "udp://";
const std::string Utility::UNIX_SCHEME = "unix://";

Address::InstanceConstSharedPtr Utility::resolveUrl(const std::string& url) {
  if (urlIsTcpScheme(url)) {
    return parseInternetAddressAndPort(url.substr(TCP_SCHEME.size()));
  } else if (urlIsUdpScheme(url)) {
    return parseInternetAddressAndPort(url.substr(UDP_SCHEME.size()));
  } else if (urlIsUnixScheme(url)) {
    return Address::InstanceConstSharedPtr{
        new Address::PipeInstance(url.substr(UNIX_SCHEME.size()))};
  } else {
    throw EnvoyException(fmt::format("unknown protocol scheme: {}", url));
  }
}

bool Utility::urlIsTcpScheme(const std::string& url) { return absl::StartsWith(url, TCP_SCHEME); }

bool Utility::urlIsUdpScheme(const std::string& url) { return absl::StartsWith(url, UDP_SCHEME); }

bool Utility::urlIsUnixScheme(const std::string& url) { return absl::StartsWith(url, UNIX_SCHEME); }

namespace {

std::string hostFromUrl(const std::string& url, const std::string& scheme,
                        const std::string& scheme_name) {
  if (!absl::StartsWith(url, scheme)) {
    throw EnvoyException(fmt::format("expected {} scheme, got: {}", scheme_name, url));
  }

  size_t colon_index = url.find(':', scheme.size());

  if (colon_index == std::string::npos) {
    throw EnvoyException(fmt::format("malformed url: {}", url));
  }

  return url.substr(scheme.size(), colon_index - scheme.size());
}

uint32_t portFromUrl(const std::string& url, const std::string& scheme,
                     const std::string& scheme_name) {
  if (!absl::StartsWith(url, scheme)) {
    throw EnvoyException(fmt::format("expected {} scheme, got: {}", scheme_name, url));
  }

  size_t colon_index = url.find(':', scheme.size());

  if (colon_index == std::string::npos) {
    throw EnvoyException(fmt::format("malformed url: {}", url));
  }

  size_t rcolon_index = url.rfind(':');
  if (colon_index != rcolon_index) {
    throw EnvoyException(fmt::format("malformed url: {}", url));
  }

  try {
    return std::stoi(url.substr(colon_index + 1));
  } catch (const std::invalid_argument& e) {
    throw EnvoyException(e.what());
  } catch (const std::out_of_range& e) {
    throw EnvoyException(e.what());
  }
}

} // namespace

std::string Utility::hostFromTcpUrl(const std::string& url) {
  return hostFromUrl(url, TCP_SCHEME, "TCP");
}

uint32_t Utility::portFromTcpUrl(const std::string& url) {
  return portFromUrl(url, TCP_SCHEME, "TCP");
}

std::string Utility::hostFromUdpUrl(const std::string& url) {
  return hostFromUrl(url, UDP_SCHEME, "UDP");
}

uint32_t Utility::portFromUdpUrl(const std::string& url) {
  return portFromUrl(url, UDP_SCHEME, "UDP");
}

Address::InstanceConstSharedPtr Utility::parseInternetAddress(const std::string& ip_address,
                                                              uint16_t port, bool v6only) {
  sockaddr_in sa4;
  if (inet_pton(AF_INET, ip_address.c_str(), &sa4.sin_addr) == 1) {
    sa4.sin_family = AF_INET;
    sa4.sin_port = htons(port);
    return std::make_shared<Address::Ipv4Instance>(&sa4);
  }
  sockaddr_in6 sa6;
  memset(&sa6, 0, sizeof(sa6));
  if (inet_pton(AF_INET6, ip_address.c_str(), &sa6.sin6_addr) == 1) {
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(port);
    return std::make_shared<Address::Ipv6Instance>(sa6, v6only);
  }
  throwWithMalformedIp(ip_address);
  NOT_REACHED_GCOVR_EXCL_LINE;
}

Address::InstanceConstSharedPtr Utility::parseInternetAddressAndPort(const std::string& ip_address,
                                                                     bool v6only) {
  if (ip_address.empty()) {
    throwWithMalformedIp(ip_address);
  }
  if (ip_address[0] == '[') {
    // Appears to be an IPv6 address. Find the "]:" that separates the address from the port.
    const auto pos = ip_address.rfind("]:");
    if (pos == std::string::npos) {
      throwWithMalformedIp(ip_address);
    }
    const auto ip_str = ip_address.substr(1, pos - 1);
    const auto port_str = ip_address.substr(pos + 2);
    uint64_t port64 = 0;
    if (port_str.empty() || !absl::SimpleAtoi(port_str, &port64) || port64 > 65535) {
      throwWithMalformedIp(ip_address);
    }
    sockaddr_in6 sa6;
    memset(&sa6, 0, sizeof(sa6));
    if (ip_str.empty() || inet_pton(AF_INET6, ip_str.c_str(), &sa6.sin6_addr) != 1) {
      throwWithMalformedIp(ip_address);
    }
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(port64);
    return std::make_shared<Address::Ipv6Instance>(sa6, v6only);
  }
  // Treat it as an IPv4 address followed by a port.
  const auto pos = ip_address.rfind(':');
  if (pos == std::string::npos) {
    throwWithMalformedIp(ip_address);
  }
  const auto ip_str = ip_address.substr(0, pos);
  const auto port_str = ip_address.substr(pos + 1);
  uint64_t port64 = 0;
  if (port_str.empty() || !absl::SimpleAtoi(port_str, &port64) || port64 > 65535) {
    throwWithMalformedIp(ip_address);
  }
  sockaddr_in sa4;
  if (ip_str.empty() || inet_pton(AF_INET, ip_str.c_str(), &sa4.sin_addr) != 1) {
    throwWithMalformedIp(ip_address);
  }
  sa4.sin_family = AF_INET;
  sa4.sin_port = htons(port64);
  return std::make_shared<Address::Ipv4Instance>(&sa4);
}

Address::InstanceConstSharedPtr Utility::copyInternetAddressAndPort(const Address::Ip& ip) {
  if (ip.version() == Address::IpVersion::v4) {
    return std::make_shared<Address::Ipv4Instance>(ip.addressAsString(), ip.port());
  }
  return std::make_shared<Address::Ipv6Instance>(ip.addressAsString(), ip.port());
}

void Utility::throwWithMalformedIp(const std::string& ip_address) {
  throw EnvoyException(fmt::format("malformed IP address: {}", ip_address));
}

// TODO(hennna): Currently getLocalAddress does not support choosing between
// multiple interfaces and addresses not returned by getifaddrs. In addition,
// the default is to return a loopback address of type version. This function may
// need to be updated in the future. Discussion can be found at Github issue #939.
Address::InstanceConstSharedPtr Utility::getLocalAddress(const Address::IpVersion version) {
  struct ifaddrs* ifaddr;
  struct ifaddrs* ifa;
  Address::InstanceConstSharedPtr ret;

  int rc = getifaddrs(&ifaddr);
  RELEASE_ASSERT(!rc, "");

  // man getifaddrs(3)
  for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr) {
      continue;
    }

    if ((ifa->ifa_addr->sa_family == AF_INET && version == Address::IpVersion::v4) ||
        (ifa->ifa_addr->sa_family == AF_INET6 && version == Address::IpVersion::v6)) {
      const struct sockaddr_storage* addr =
          reinterpret_cast<const struct sockaddr_storage*>(ifa->ifa_addr);
      ret = Address::addressFromSockAddr(
          *addr, (version == Address::IpVersion::v4) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));
      if (!isLoopbackAddress(*ret)) {
        break;
      }
    }
  }

  if (ifaddr) {
    freeifaddrs(ifaddr);
  }

  // If the local address is not found above, then return the loopback address by default.
  if (ret == nullptr) {
    if (version == Address::IpVersion::v4) {
      ret.reset(new Address::Ipv4Instance("127.0.0.1"));
    } else if (version == Address::IpVersion::v6) {
      ret.reset(new Address::Ipv6Instance("::1"));
    }
  }
  return ret;
}

bool Utility::isLocalConnection(const Network::ConnectionSocket& socket) {
  const auto& remote_address = socket.remoteAddress();
  // Before calling getifaddrs, verify the obvious checks.
  // Note that there are corner cases, where remote and local address will be the same
  // while the client is not actually local. Example could be an iptables intercepted
  // connection. However, this is a rare exception and such assumption results in big
  // performance optimization.
  if (remote_address->type() == Envoy::Network::Address::Type::Pipe ||
      remote_address == socket.localAddress() || isLoopbackAddress(*remote_address)) {
    return true;
  }

  struct ifaddrs* ifaddr;
  const int rc = getifaddrs(&ifaddr);
  Cleanup ifaddr_cleanup([ifaddr] {
    if (ifaddr) {
      freeifaddrs(ifaddr);
    }
  });
  RELEASE_ASSERT(rc == 0, "");

  const auto af_look_up =
      (remote_address->ip()->version() == Address::IpVersion::v4) ? AF_INET : AF_INET6;

  for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr) {
      continue;
    }

    if (ifa->ifa_addr->sa_family == af_look_up) {
      const auto* addr = reinterpret_cast<const struct sockaddr_storage*>(ifa->ifa_addr);
      const auto local_address = Address::addressFromSockAddr(
          *addr, (af_look_up == AF_INET) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));

      if (remote_address == local_address) {
        return true;
      }
    }
  }

  return false;
}

bool Utility::isInternalAddress(const Address::Instance& address) {
  if (address.type() != Address::Type::Ip) {
    return false;
  }

  if (address.ip()->version() == Address::IpVersion::v4) {
    // Handle the RFC1918 space for IPV4. Also count loopback as internal.
    const uint32_t address4 = address.ip()->ipv4()->address();
    const uint8_t* address4_bytes = reinterpret_cast<const uint8_t*>(&address4);
    if ((address4_bytes[0] == 10) || (address4_bytes[0] == 192 && address4_bytes[1] == 168) ||
        (address4_bytes[0] == 172 && address4_bytes[1] >= 16 && address4_bytes[1] <= 31) ||
        address4 == htonl(INADDR_LOOPBACK)) {
      return true;
    } else {
      return false;
    }
  }

  // Local IPv6 address prefix defined in RFC4193. Local addresses have prefix FC00::/7.
  // Currently, the FD00::/8 prefix is locally assigned and FC00::/8 may be defined in the
  // future.
  static_assert(sizeof(absl::uint128) == sizeof(in6addr_loopback),
                "sizeof(absl::uint128) != sizeof(in6addr_loopback)");
  const absl::uint128 address6 = address.ip()->ipv6()->address();
  const uint8_t* address6_bytes = reinterpret_cast<const uint8_t*>(&address6);
  if (address6_bytes[0] == 0xfd ||
      memcmp(&address6, &in6addr_loopback, sizeof(in6addr_loopback)) == 0) {
    return true;
  }

  return false;
}

bool Utility::isLoopbackAddress(const Address::Instance& address) {
  if (address.type() != Address::Type::Ip) {
    return false;
  }

  if (address.ip()->version() == Address::IpVersion::v4) {
    // Compare to the canonical v4 loopback address: 127.0.0.1.
    return address.ip()->ipv4()->address() == htonl(INADDR_LOOPBACK);
  } else if (address.ip()->version() == Address::IpVersion::v6) {
    static_assert(sizeof(absl::uint128) == sizeof(in6addr_loopback),
                  "sizeof(absl::uint128) != sizeof(in6addr_loopback)");
    absl::uint128 addr = address.ip()->ipv6()->address();
    return 0 == memcmp(&addr, &in6addr_loopback, sizeof(in6addr_loopback));
  }
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

Address::InstanceConstSharedPtr Utility::getCanonicalIpv4LoopbackAddress() {
  CONSTRUCT_ON_FIRST_USE(Address::InstanceConstSharedPtr,
                         new Address::Ipv4Instance("127.0.0.1", 0));
}

Address::InstanceConstSharedPtr Utility::getIpv6LoopbackAddress() {
  CONSTRUCT_ON_FIRST_USE(Address::InstanceConstSharedPtr, new Address::Ipv6Instance("::1", 0));
}

Address::InstanceConstSharedPtr Utility::getIpv4AnyAddress() {
  CONSTRUCT_ON_FIRST_USE(Address::InstanceConstSharedPtr,
                         new Address::Ipv4Instance(static_cast<uint32_t>(0)));
}

Address::InstanceConstSharedPtr Utility::getIpv6AnyAddress() {
  CONSTRUCT_ON_FIRST_USE(Address::InstanceConstSharedPtr,
                         new Address::Ipv6Instance(static_cast<uint32_t>(0)));
}

const std::string& Utility::getIpv4CidrCatchAllAddress() {
  CONSTRUCT_ON_FIRST_USE(std::string, "0.0.0.0/0");
}

const std::string& Utility::getIpv6CidrCatchAllAddress() {
  CONSTRUCT_ON_FIRST_USE(std::string, "::/0");
}

Address::InstanceConstSharedPtr Utility::getAddressWithPort(const Address::Instance& address,
                                                            uint32_t port) {
  switch (address.ip()->version()) {
  case Network::Address::IpVersion::v4:
    return std::make_shared<Address::Ipv4Instance>(address.ip()->addressAsString(), port);
  case Network::Address::IpVersion::v6:
    return std::make_shared<Address::Ipv6Instance>(address.ip()->addressAsString(), port);
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

Address::InstanceConstSharedPtr Utility::getOriginalDst(int fd) {
#ifdef SOL_IP
  sockaddr_storage orig_addr;
  socklen_t addr_len = sizeof(sockaddr_storage);
  int socket_domain;
  socklen_t domain_len = sizeof(socket_domain);
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Api::SysCallIntResult result =
      os_syscalls.getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &socket_domain, &domain_len);
  int status = result.rc_;

  if (status != 0) {
    return nullptr;
  }

  if (socket_domain == AF_INET) {
    status = os_syscalls.getsockopt(fd, SOL_IP, SO_ORIGINAL_DST, &orig_addr, &addr_len).rc_;
  } else if (socket_domain == AF_INET6) {
    status = os_syscalls.getsockopt(fd, SOL_IPV6, IP6T_SO_ORIGINAL_DST, &orig_addr, &addr_len).rc_;
  } else {
    return nullptr;
  }

  if (status != 0) {
    return nullptr;
  }

  switch (orig_addr.ss_family) {
  case AF_INET:
    return Address::InstanceConstSharedPtr{
        new Address::Ipv4Instance(reinterpret_cast<sockaddr_in*>(&orig_addr))};
  case AF_INET6:
    return Address::InstanceConstSharedPtr{
        new Address::Ipv6Instance(reinterpret_cast<sockaddr_in6&>(orig_addr))};
  default:
    return nullptr;
  }
#else
  // TODO(zuercher): determine if connection redirection is possible under macOS (c.f. pfctl and
  // divert), and whether it's possible to find the learn destination address.
  UNREFERENCED_PARAMETER(fd);
  return nullptr;
#endif
}

void Utility::parsePortRangeList(absl::string_view string, std::list<PortRange>& list) {
  const auto ranges = StringUtil::splitToken(string, ",");
  for (const auto& s : ranges) {
    const std::string s_string{s};
    std::stringstream ss(s_string);
    uint32_t min = 0;
    uint32_t max = 0;

    if (s.find("-") != std::string::npos) {
      char dash = 0;
      ss >> min;
      ss >> dash;
      ss >> max;
    } else {
      ss >> min;
      max = min;
    }

    if (s.empty() || (min > 65535) || (max > 65535) || ss.fail() || !ss.eof()) {
      throw EnvoyException(fmt::format("invalid port number or range '{}'", s_string));
    }

    list.emplace_back(PortRange(min, max));
  }
}

bool Utility::portInRangeList(const Address::Instance& address, const std::list<PortRange>& list) {
  if (address.type() != Address::Type::Ip) {
    return false;
  }

  for (const Network::PortRange& p : list) {
    if (p.contains(address.ip()->port())) {
      return true;
    }
  }
  return false;
}

absl::uint128 Utility::Ip6ntohl(const absl::uint128& address) {
  // TODO(ccaraman): Support Ip6ntohl for big-endian.
  static_assert(ABSL_IS_LITTLE_ENDIAN,
                "Machines using big-endian byte order is not supported for IPv6.");
  return flipOrder(address);
}

absl::uint128 Utility::Ip6htonl(const absl::uint128& address) {
  // TODO(ccaraman): Support Ip6ntohl for big-endian.
  static_assert(ABSL_IS_LITTLE_ENDIAN,
                "Machines using big-endian byte order is not supported for IPv6.");
  return flipOrder(address);
}

absl::uint128 Utility::flipOrder(const absl::uint128& input) {
  absl::uint128 result{0};
  absl::uint128 data = input;
  for (int i = 0; i < 16; i++) {
    result <<= 8;
    result |= (data & 0x000000000000000000000000000000FF);
    data >>= 8;
  }
  return result;
}

Address::InstanceConstSharedPtr
Utility::protobufAddressToAddress(const envoy::api::v2::core::Address& proto_address) {
  switch (proto_address.address_case()) {
  case envoy::api::v2::core::Address::kSocketAddress:
    return Network::Utility::parseInternetAddress(proto_address.socket_address().address(),
                                                  proto_address.socket_address().port_value(),
                                                  !proto_address.socket_address().ipv4_compat());
  case envoy::api::v2::core::Address::kPipe:
    return std::make_shared<Address::PipeInstance>(proto_address.pipe().path());
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

void Utility::addressToProtobufAddress(const Address::Instance& address,
                                       envoy::api::v2::core::Address& proto_address) {
  if (address.type() == Address::Type::Pipe) {
    proto_address.mutable_pipe()->set_path(address.asString());
  } else {
    ASSERT(address.type() == Address::Type::Ip);
    auto* socket_address = proto_address.mutable_socket_address();
    socket_address->set_address(address.ip()->addressAsString());
    socket_address->set_port_value(address.ip()->port());
  }
}

Address::SocketType
Utility::protobufAddressSocketType(const envoy::api::v2::core::Address& proto_address) {
  switch (proto_address.address_case()) {
  case envoy::api::v2::core::Address::kSocketAddress: {
    const auto protocol = proto_address.socket_address().protocol();
    switch (protocol) {
    case envoy::api::v2::core::SocketAddress::TCP:
      return Address::SocketType::Stream;
    case envoy::api::v2::core::SocketAddress::UDP:
      return Address::SocketType::Datagram;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
  case envoy::api::v2::core::Address::kPipe:
    return Address::SocketType::Stream;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Network
} // namespace Envoy
