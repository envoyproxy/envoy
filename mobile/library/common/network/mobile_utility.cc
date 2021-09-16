#include "library/common/network/mobile_utility.h"

#include "envoy/common/platform.h"

#include "source/common/common/assert.h"
#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/common/utility.h"
#include "source/common/network/socket_option_impl.h"

// Used on Linux/Android
#ifdef SO_BINDTODEVICE
#define ENVOY_SOCKET_SO_BINDTODEVICE ENVOY_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_BINDTODEVICE)
#else
#define ENVOY_SOCKET_SO_BINDTODEVICE Network::SocketOptionName()
#endif

// Used on BSD/iOS
#ifdef IP_BOUND_IF
#define ENVOY_SOCKET_IP_BOUND_IF ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_IP, IP_BOUND_IF)
#else
#define ENVOY_SOCKET_IP_BOUND_IF Network::SocketOptionName()
#endif

#ifdef IPV6_BOUND_IF
#define ENVOY_SOCKET_IPV6_BOUND_IF ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_IPV6, IPV6_BOUND_IF)
#else
#define ENVOY_SOCKET_IPV6_BOUND_IF Network::SocketOptionName()
#endif

// Dummy/test option
#ifdef IP_TTL
#define ENVOY_SOCKET_IP_TTL ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_IP, IP_TTL)
#else
#define ENVOY_SOCKET_IP_TTL Network::SocketOptionName()
#endif

#define DEFAULT_IP_TTL 64

#ifdef SUPPORTS_GETIFADDRS
#include <ifaddrs.h>
#endif

namespace Envoy {
namespace Network {

#if !defined(SUPPORTS_GETIFADDRS) && defined(INCLUDE_IFADDRS)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
namespace {
#include "third_party/android/ifaddrs-android.h"
}
#pragma clang diagnostic pop
#define SUPPORTS_GETIFADDRS
#endif

std::atomic<envoy_network_t> MobileUtility::preferred_network_{ENVOY_NET_GENERIC};

void MobileUtility::setPreferredNetwork(envoy_network_t network) { preferred_network_ = network; }

envoy_network_t MobileUtility::getPreferredNetwork() { return preferred_network_.load(); }

std::vector<std::string> MobileUtility::enumerateV4Interfaces() {
  return enumerateInterfaces(AF_INET);
}

std::vector<std::string> MobileUtility::enumerateV6Interfaces() {
  return enumerateInterfaces(AF_INET6);
}

Socket::OptionsSharedPtr MobileUtility::getUpstreamSocketOptions(envoy_network_t network) {
  // Envoy uses the hash signature of overridden socket options to choose a connection pool.
  // Setting a dummy socket option is a hack that allows us to select a different
  // connection pool without materially changing the socket configuration.
  ASSERT(network >= 0 && network < 3);
  int ttl_value = DEFAULT_IP_TTL + static_cast<int>(network);
  auto options = std::make_shared<Socket::Options>();
  options->push_back(std::make_shared<SocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_PREBIND, ENVOY_SOCKET_IP_TTL, ttl_value));
  return options;
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
#endif // SUPPORTS_GETIFADDRS

  return names;
}

} // namespace Network
} // namespace Envoy
