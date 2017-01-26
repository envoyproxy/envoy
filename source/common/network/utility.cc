#include "utility.h"

#include "envoy/common/exception.h"
#include "envoy/network/connection.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"
#include "common/common/utility.h"

#include <ifaddrs.h>

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

bool IpList::contains(const std::string& remote_address) const {
  in_addr addr;
  int rc = inet_pton(AF_INET, remote_address.c_str(), &addr);
  if (1 != rc) {
    return false;
  }

  for (const Ipv4Entry& entry : ipv4_list_) {
    if ((ntohl(addr.s_addr) & entry.ipv4_mask_) == entry.ipv4_address_) {
      return true;
    }
  }

  return false;
}

IpWhiteList::IpWhiteList(const Json::Object& config)
    : ip_list_(config.hasObject("ip_white_list") ? config.getStringArray("ip_white_list")
                                                 : std::vector<std::string>()) {}

const std::string Utility::TCP_SCHEME = "tcp://";
const std::string Utility::UNIX_SCHEME = "unix://";

AddrInfoPtr Utility::resolveTCP(const std::string& host, uint32_t port) {
  addrinfo addrinfo_hints;
  memset(&addrinfo_hints, 0, sizeof(addrinfo_hints));
  addrinfo_hints.ai_family = AF_INET;
  addrinfo_hints.ai_socktype = SOCK_STREAM;
  addrinfo_hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

  const char* host_to_use;
  if (!host.empty()) {
    host_to_use = host.c_str();
  } else {
    host_to_use = nullptr;
    addrinfo_hints.ai_flags |= AI_PASSIVE;
  }

  std::string port_string = std::to_string(port);
  addrinfo* addrinfo_out;
  if (0 != getaddrinfo(host_to_use, port_string.c_str(), &addrinfo_hints, &addrinfo_out)) {
    throw EnvoyException(fmt::format("unable to resolve host {} : {}", host, gai_strerror(errno)));
  }

  return AddrInfoPtr{addrinfo_out};
}

sockaddr_un Utility::resolveUnixDomainSocket(const std::string& path) {
  sockaddr_un address;
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  StringUtil::strlcpy(&address.sun_path[0], path.c_str(), sizeof(address.sun_path));
  return address;
}

void Utility::resolve(const std::string& url) {
  if (url.find(TCP_SCHEME) == 0) {
    resolveTCP(hostFromUrl(url), portFromUrl(url));
  } else if (url.find(UNIX_SCHEME) == 0) {
    resolveUnixDomainSocket(pathFromUrl(url));
  } else {
    throw EnvoyException(fmt::format("unknown protocol scheme: {}", url));
  }
}

std::string Utility::hostFromUrl(const std::string& url) {
  if (url.find(TCP_SCHEME) != 0) {
    throw EnvoyException(fmt::format("unknown protocol scheme: {}", url));
  }

  size_t colon_index = url.find(':', TCP_SCHEME.size());

  if (colon_index == std::string::npos) {
    throw EnvoyException(fmt::format("malformed url: {}", url));
  }

  return url.substr(TCP_SCHEME.size(), colon_index - TCP_SCHEME.size());
}

uint32_t Utility::portFromUrl(const std::string& url) {
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

std::string Utility::pathFromUrl(const std::string& url) {
  if (url.find(UNIX_SCHEME) != 0) {
    throw EnvoyException(fmt::format("unknown protocol scheme: {}", url));
  }

  return url.substr(UNIX_SCHEME.size());
}

std::string Utility::urlForTcp(const std::string& address, uint32_t port) {
  return fmt::format("{}{}:{}", TCP_SCHEME, address, port);
}

std::string Utility::getLocalAddress() {
  struct ifaddrs* ifaddr;
  struct ifaddrs* ifa;
  std::string ret;

  int rc = getifaddrs(&ifaddr);
  RELEASE_ASSERT(!rc);
  UNREFERENCED_PARAMETER(rc);

  // man getifaddrs(3)
  for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr) {
      continue;
    }

    if (ifa->ifa_addr->sa_family == AF_INET) {
      sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
      if (htonl(INADDR_LOOPBACK) != addr->sin_addr.s_addr) {
        ret = getAddressName(addr);
        break;
      }
    }
  }

  if (ifaddr) {
    freeifaddrs(ifaddr);
  }

  return ret;
}

std::string Utility::getAddressName(sockaddr_in* addr) {
  char str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &addr->sin_addr, str, INET_ADDRSTRLEN);
  return std::string(str);
}

uint16_t Utility::getAddressPort(sockaddr_in* addr) { return ntohs(addr->sin_port); }

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

bool Utility::isLoopbackAddress(const char* address) {
  in_addr addr;
  int rc = inet_pton(AF_INET, address, &addr);
  if (1 != rc) {
    return false;
  }

  return addr.s_addr == htonl(INADDR_LOOPBACK);
}

bool Utility::getOriginalDst(int fd, sockaddr_storage* orig_addr) {
  socklen_t addr_len = sizeof(sockaddr_storage);
  int status = getsockopt(fd, SOL_IP, SO_ORIGINAL_DST, orig_addr, &addr_len);

  return (status == 0);
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

    list.emplace_back(PortRange(min, max));
  }
}

bool Utility::portInRangeList(uint32_t port, const std::list<PortRange>& list) {
  for (const Network::PortRange& p : list) {
    if (p.contains(port)) {
      return true;
    }
  }
  return false;
}

} // Network
