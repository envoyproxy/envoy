#include "common/network/utility.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/netfilter_ipv4.h>
#include <netinet/ip.h>
#include <sys/socket.h>

#include <cstdint>
#include <list>
#include <sstream>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/network/connection.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/network/address_impl.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Network {

IpList::IpList(const std::vector<std::string>& subnets) {
  for (const std::string& entry : subnets) {
    std::vector<std::string> parts = StringUtil::split(entry, '/');
    if (parts.size() != 2) {
      throw EnvoyException(
          fmt::format("invalid ipv4/mask combo '{}' (format is <ip>/<# mask bits>)", entry));
    }

    in_addr addr;
    int rc = inet_pton(AF_INET, parts[0].c_str(), &addr);
    if (1 != rc) {
      throw EnvoyException(fmt::format("invalid ipv4/mask combo '{}' (invalid IP address)", entry));
    }

    // "0.0.0.0/0" is a valid subnet that contains all possible IPv4 addresses,
    // so mask can be equal to 0
    uint64_t mask;
    if (!StringUtil::atoul(parts[1].c_str(), mask) || mask > 32) {
      throw EnvoyException(
          fmt::format("invalid ipv4/mask combo '{}' (mask bits must be <= 32)", entry));
    }

    Ipv4Entry list_entry;
    list_entry.ipv4_address_ = ntohl(addr.s_addr);
    // The 1ULL below makes sure that the RHS is computed as a 64-bit value, so that we do not
    // over-shift to the left when mask = 0. The assignment to ipv4_mask_ then truncates
    // the value back to 32 bits.
    list_entry.ipv4_mask_ = ~((1ULL << (32 - mask)) - 1);

    // Check to make sure applying the mask to the address equals the address. This can prevent
    // user error.
    if ((list_entry.ipv4_address_ & list_entry.ipv4_mask_) != list_entry.ipv4_address_) {
      throw EnvoyException(
          fmt::format("invalid ipv4/mask combo '{}' ((address & mask) != address)", entry));
    }

    ipv4_list_.push_back(list_entry);
  }
}

bool IpList::contains(const Address::Instance& address) const {
  if (address.type() != Address::Type::Ip) {
    return false;
  }

  for (const Ipv4Entry& entry : ipv4_list_) {
    // TODO(mattklein123): IPv6 support
    if ((ntohl(address.ip()->ipv4()->address()) & entry.ipv4_mask_) == entry.ipv4_address_) {
      return true;
    }
  }

  return false;
}

IpList::IpList(const Json::Object& config, const std::string& member_name)
    : IpList(config.hasObject(member_name) ? config.getStringArray(member_name)
                                           : std::vector<std::string>()) {}

const std::string Utility::TCP_SCHEME = "tcp://";
const std::string Utility::UNIX_SCHEME = "unix://";

Address::InstanceConstSharedPtr Utility::resolveUrl(const std::string& url) {
  if (url.find(TCP_SCHEME) == 0) {
    return parseInternetAddressAndPort(url.substr(TCP_SCHEME.size()));
  } else if (url.find(UNIX_SCHEME) == 0) {
    return Address::InstanceConstSharedPtr{
        new Address::PipeInstance(url.substr(UNIX_SCHEME.size()))};
  } else {
    throw EnvoyException(fmt::format("unknown protocol scheme: {}", url));
  }
}

std::string Utility::hostFromTcpUrl(const std::string& url) {
  if (url.find(TCP_SCHEME) != 0) {
    throw EnvoyException(fmt::format("unknown protocol scheme: {}", url));
  }

  size_t colon_index = url.find(':', TCP_SCHEME.size());

  if (colon_index == std::string::npos) {
    throw EnvoyException(fmt::format("malformed url: {}", url));
  }

  return url.substr(TCP_SCHEME.size(), colon_index - TCP_SCHEME.size());
}

uint32_t Utility::portFromTcpUrl(const std::string& url) {
  if (url.find(TCP_SCHEME) != 0) {
    throw EnvoyException(fmt::format("unknown protocol scheme: {}", url));
  }

  size_t colon_index = url.find(':', TCP_SCHEME.size());

  if (colon_index == std::string::npos) {
    throw EnvoyException(fmt::format("malformed url: {}", url));
  }

  try {
    return std::stoi(url.substr(colon_index + 1));
  } catch (const std::invalid_argument& e) {
    throw EnvoyException(e.what());
  }
}

Address::InstanceConstSharedPtr Utility::parseInternetAddress(const std::string& ip_address) {
  sockaddr_in sa4;
  if (inet_pton(AF_INET, ip_address.c_str(), &sa4.sin_addr) == 1) {
    sa4.sin_family = AF_INET;
    sa4.sin_port = 0;
    return std::make_shared<Address::Ipv4Instance>(&sa4);
  }
  sockaddr_in6 sa6;
  if (inet_pton(AF_INET6, ip_address.c_str(), &sa6.sin6_addr) == 1) {
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = 0;
    return std::make_shared<Address::Ipv6Instance>(sa6);
  }
  throwWithMalformedIp(ip_address);
  NOT_REACHED;
}

Address::InstanceConstSharedPtr
Utility::parseInternetAddressAndPort(const std::string& ip_address) {
  if (ip_address.empty()) {
    throwWithMalformedIp(ip_address);
  }
  if (ip_address[0] == '[') {
    // Appears to be an IPv6 address. Find the "]:" that separates the address from the port.
    auto pos = ip_address.rfind("]:");
    if (pos == std::string::npos) {
      throwWithMalformedIp(ip_address);
    }
    const auto ip_str = ip_address.substr(1, pos - 1);
    const auto port_str = ip_address.substr(pos + 2);
    uint64_t port64 = 0;
    if (port_str.empty() || !StringUtil::atoul(port_str.c_str(), port64, 10) || port64 > 65535) {
      throwWithMalformedIp(ip_address);
    }
    sockaddr_in6 sa6;
    if (ip_str.empty() || inet_pton(AF_INET6, ip_str.c_str(), &sa6.sin6_addr) != 1) {
      throwWithMalformedIp(ip_address);
    }
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(port64);
    return std::make_shared<Address::Ipv6Instance>(sa6);
  }
  // Treat it as an IPv4 address followed by a port.
  auto pos = ip_address.rfind(":");
  if (pos == std::string::npos) {
    throwWithMalformedIp(ip_address);
  }
  const auto ip_str = ip_address.substr(0, pos);
  const auto port_str = ip_address.substr(pos + 1);
  uint64_t port64 = 0;
  if (port_str.empty() || !StringUtil::atoul(port_str.c_str(), port64, 10) || port64 > 65535) {
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

void Utility::throwWithMalformedIp(const std::string& ip_address) {
  throw EnvoyException(fmt::format("malformed IP address: {}", ip_address));
}

// TODO(hennna): Currently getLocalAddress does not support choosing between
// multiple interfaces and addresses not returned by getifaddrs. In additon,
// the default is to return a loopback address of type version. This function may
// need to be updated in the future. Discussion can be found at Github issue #939.
Address::InstanceConstSharedPtr Utility::getLocalAddress(const Address::IpVersion version) {
  struct ifaddrs* ifaddr;
  struct ifaddrs* ifa;
  Address::InstanceConstSharedPtr ret;

  int rc = getifaddrs(&ifaddr);
  RELEASE_ASSERT(!rc);
  UNREFERENCED_PARAMETER(rc);

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

  // If the local address is not found above, then return the loopback addresss by default.
  if (ret == nullptr) {
    if (version == Address::IpVersion::v4) {
      ret.reset(new Address::Ipv4Instance("127.0.0.1"));
    } else if (version == Address::IpVersion::v6) {
      ret.reset(new Address::Ipv6Instance("::1"));
    }
  }
  return ret;
}

bool Utility::isInternalAddress(const char* address) {
  in_addr addr;
  int rc = inet_pton(AF_INET, address, &addr);
  if (1 != rc) {
    return false;
  }

  // Handle the RFC1918 space for IPV4. Also count loopback as internal.
  uint8_t* address_bytes = reinterpret_cast<uint8_t*>(&addr.s_addr);
  if ((address_bytes[0] == 10) || (address_bytes[0] == 192 && address_bytes[1] == 168) ||
      (address_bytes[0] == 172 && address_bytes[1] >= 16 && address_bytes[1] <= 31) ||
      addr.s_addr == htonl(INADDR_LOOPBACK)) {
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
    std::array<uint8_t, 16> addr = address.ip()->ipv6()->address();
    return 0 == memcmp(&addr, &in6addr_loopback, sizeof(in6addr_loopback));
  }
  NOT_IMPLEMENTED;
}

Address::InstanceConstSharedPtr Utility::getCanonicalIpv4LoopbackAddress() {
  // Initialized on first call in a thread-safe manner.
  static Address::InstanceConstSharedPtr loopback(new Address::Ipv4Instance("127.0.0.1", 0));
  return loopback;
}

Address::InstanceConstSharedPtr Utility::getIpv6LoopbackAddress() {
  // Initialized on first call in a thread-safe manner.
  static Address::InstanceConstSharedPtr loopback(new Address::Ipv6Instance("::1", 0));
  return loopback;
}

Address::InstanceConstSharedPtr Utility::getIpv4AnyAddress() {
  // Initialized on first call in a thread-safe manner.
  static Address::InstanceConstSharedPtr any(new Address::Ipv4Instance(static_cast<uint32_t>(0)));
  return any;
}

Address::InstanceConstSharedPtr Utility::getIpv6AnyAddress() {
  // Initialized on first call in a thread-safe manner.
  static Address::InstanceConstSharedPtr any(new Address::Ipv6Instance(static_cast<uint32_t>(0)));
  return any;
}

Address::InstanceConstSharedPtr Utility::getAddressUpdatePort(const Address::Instance& address,
                                                              uint32_t port) {
  switch (address.ip()->version()) {
  case Network::Address::IpVersion::v4:
    return std::make_shared<Address::Ipv4Instance>(address.ip()->addressAsString(), port);
  case Network::Address::IpVersion::v6:
    return std::make_shared<Address::Ipv6Instance>(address.ip()->addressAsString(), port);
  }
  NOT_REACHED;
}

Address::InstanceConstSharedPtr Utility::getOriginalDst(int fd) {
  sockaddr_storage orig_addr;
  socklen_t addr_len = sizeof(sockaddr_storage);
  int status = getsockopt(fd, SOL_IP, SO_ORIGINAL_DST, &orig_addr, &addr_len);

  if (status == 0) {
    // TODO(mattklein123): IPv6 support
    ASSERT(orig_addr.ss_family == AF_INET);
    return Address::InstanceConstSharedPtr{
        new Address::Ipv4Instance(reinterpret_cast<sockaddr_in*>(&orig_addr))};
  } else {
    return nullptr;
  }
}

void Utility::parsePortRangeList(const std::string& string, std::list<PortRange>& list) {
  std::vector<std::string> ranges = StringUtil::split(string.c_str(), ',');
  for (const std::string& s : ranges) {
    std::stringstream ss(s);
    uint32_t min = 0;
    uint32_t max = 0;

    if (s.find('-') != std::string::npos) {
      char dash = 0;
      ss >> min;
      ss >> dash;
      ss >> max;
    } else {
      ss >> min;
      max = min;
    }

    if (s.empty() || (min > 65535) || (max > 65535) || ss.fail() || !ss.eof()) {
      throw EnvoyException(fmt::format("invalid port number or range '{}'", s));
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

} // Network
} // Envoy
