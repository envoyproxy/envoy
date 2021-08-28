#include "library/common/network/mobile_utility.h"

#include "envoy/common/platform.h"

#include "source/common/common/assert.h"

#ifdef SUPPORTS_GETIFADDRS
#include <ifaddrs.h>
#endif

namespace Envoy {
namespace Network {

std::vector<std::string> MobileUtility::enumerateV4Interfaces() {
  return enumerateInterfaces(AF_INET);
}

std::vector<std::string> MobileUtility::enumerateV6Interfaces() {
  return enumerateInterfaces(AF_INET6);
}

std::vector<std::string>
MobileUtility::enumerateInterfaces([[maybe_unused]] unsigned short family) {
  std::vector<std::string> names{};

#ifdef SUPPORTS_GETIFADDRS
  struct ifaddrs* interfaces = nullptr;
  struct ifaddrs* ifa = nullptr;

  const int rc = getifaddrs(&interfaces);
  RELEASE_ASSERT(!rc, "getifaddrs failed");

  for (ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr && ifa->ifa_addr->sa_family == family) {
      names.push_back(std::string{ifa->ifa_name});
    }
  }

  freeifaddrs(interfaces);
#endif

  return names;
}

} // namespace Network
} // namespace Envoy
