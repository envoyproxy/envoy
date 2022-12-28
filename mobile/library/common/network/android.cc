#include "library/common/network/android.h"

#include <net/if.h>

#include "envoy/common/platform.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/common/utility.h"
#include "source/common/network/addr_family_aware_socket_option_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"

#include "library/common/network/src_addr_socket_option_impl.h"

namespace Envoy {
namespace Network {
namespace Android {
namespace Utility {

#if defined(INCLUDE_IFADDRS)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
namespace {
#include "third_party/android/ifaddrs-android.h"
}
#pragma clang diagnostic pop
#endif

void setAlternateGetifaddrs() {
#if defined(INCLUDE_IFADDRS)
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  ENVOY_BUG(!os_syscalls.supportsGetifaddrs(),
            "setAlternateGetifaddrs should only be called when supportsGetifaddrs is false");

  Api::AlternateGetifaddrs android_getifaddrs =
      [](Api::InterfaceAddressVector& interfaces) -> Api::SysCallIntResult {
    struct ifaddrs* ifaddr;
    struct ifaddrs* ifa;

    const int rc = getifaddrs(&ifaddr);
    if (rc == -1) {
      return {rc, errno};
    }

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == nullptr) {
        continue;
      }

      if (ifa->ifa_addr->sa_family == AF_INET || ifa->ifa_addr->sa_family == AF_INET6) {
        const sockaddr_storage* ss = reinterpret_cast<sockaddr_storage*>(ifa->ifa_addr);
        size_t ss_len =
            ifa->ifa_addr->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
        StatusOr<Address::InstanceConstSharedPtr> address =
            Address::addressFromSockAddr(*ss, ss_len, ifa->ifa_addr->sa_family == AF_INET6);
        if (address.ok()) {
          interfaces.emplace_back(ifa->ifa_name, ifa->ifa_flags, *address);
        }
      }
    }

    if (ifaddr) {
      freeifaddrs(ifaddr);
    }

    return {rc, 0};
  };

  os_syscalls.setAlternateGetifaddrs(android_getifaddrs);
#endif
}

} // namespace Utility
} // namespace Android
} // namespace Network
} // namespace Envoy
