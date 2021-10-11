#include "library/common/network/configurator.h"

#include <net/if.h>

#include "envoy/common/platform.h"

#include "source/common/common/assert.h"
#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/common/utility.h"
#include "source/common/network/addr_family_aware_socket_option_impl.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"

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

#ifdef IPV6_UNICAST_HOPS
#define ENVOY_SOCKET_IPV6_UNICAST_HOPS                                                             \
  ENVOY_MAKE_SOCKET_OPTION_NAME(IPPROTO_IPV6, IPV6_UNICAST_HOPS)
#else
#define ENVOY_SOCKET_IPV6_UNICAST_HOPS Network::SocketOptionName()
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

SINGLETON_MANAGER_REGISTRATION(network_configurator);

constexpr absl::string_view BaseDnsCache = "base_dns_cache";

std::atomic<envoy_network_t> Configurator::preferred_network_{ENVOY_NET_GENERIC};

envoy_network_t Configurator::setPreferredNetwork(envoy_network_t network) {
  ENVOY_LOG_EVENT(debug, "network_configuration_network_change", std::to_string(network));
  return preferred_network_.exchange(network);
}

envoy_network_t Configurator::getPreferredNetwork() { return preferred_network_.load(); }

bool Configurator::overrideInterface(envoy_network_t) { return false; }

void Configurator::refreshDns(envoy_network_t network) {
  // refreshDns is intended to be queued on Envoy's event loop, whereas preferred_network_ is
  // updated synchronously. In the event that multiple refreshes become queued on the event loop,
  // this avoids triggering a refresh for a non-current network.
  // Note this does NOT completely prevent parallel refreshes from being triggered in multiple
  // flip-flop scenarios.
  if (network != preferred_network_.load()) {
    ENVOY_LOG_EVENT(debug, "network_configuration_dns_flipflop", std::to_string(network));
    return;
  }

  if (auto dns_cache = dns_cache_manager_->lookUpCacheByName(BaseDnsCache)) {
    ENVOY_LOG_EVENT(debug, "network_configuration_refresh_dns", std::to_string(network));
    dns_cache->forceRefreshHosts();
  } else {
    ENVOY_LOG_EVENT(warn, "network_configuration_dns_cache_missing", BaseDnsCache);
  }
}

std::vector<std::string> Configurator::enumerateV4Interfaces() {
  return enumerateInterfaces(AF_INET, 0, 0);
}

std::vector<std::string> Configurator::enumerateV6Interfaces() {
  return enumerateInterfaces(AF_INET6, 0, 0);
}

Socket::OptionsSharedPtr Configurator::getUpstreamSocketOptions(envoy_network_t network,
                                                                bool override_interface) {
  if (override_interface && network != ENVOY_NET_GENERIC) {
    return getAlternateInterfaceSocketOptions(network);
  }
  // Envoy uses the hash signature of overridden socket options to choose a connection pool.
  // Setting a dummy socket option is a hack that allows us to select a different
  // connection pool without materially changing the socket configuration.
  ASSERT(network >= 0 && network < 3);
  int ttl_value = DEFAULT_IP_TTL + static_cast<int>(network);
  auto options = std::make_shared<Socket::Options>();
  options->push_back(std::make_shared<AddrFamilyAwareSocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_PREBIND, ENVOY_SOCKET_IP_TTL,
      ENVOY_SOCKET_IPV6_UNICAST_HOPS, ttl_value));
  return options;
}

Socket::OptionsSharedPtr Configurator::getAlternateInterfaceSocketOptions(envoy_network_t network) {
  auto& v4_interface = getActiveAlternateInterface(network, AF_INET);
  auto& v6_interface = getActiveAlternateInterface(network, AF_INET6);

  auto options = std::make_shared<Socket::Options>();

  // Android
#ifdef SO_BINDTODEVICE
  options->push_back(std::make_shared<AddrFamilyAwareSocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_PREBIND, ENVOY_SOCKET_SO_BINDTODEVICE,
      v4_interface, ENVOY_SOCKET_SO_BINDTODEVICE, v6_interface));
#endif // SO_BINDTODEVICE

  // iOS
#ifdef IP_BOUND_IF
  int v4_idx = if_nametoindex(v4_interface.c_str());
  int v6_idx = if_nametoindex(v6_interface.c_str());
  options->push_back(std::make_shared<AddrFamilyAwareSocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_PREBIND, ENVOY_SOCKET_IP_BOUND_IF, v4_idx,
      ENVOY_SOCKET_IPV6_BOUND_IF, v6_idx));
#endif // IP_BOUND_IF

  return options;
}

const std::string Configurator::getActiveAlternateInterface(envoy_network_t network,
                                                            unsigned short family) {
  // Attempt to derive an active interface that differs from the passed network parameter.
  if (network == ENVOY_NET_WWAN) {
    // Network is cellular, so look for a WiFi interface.
    // WiFi should always support multicast, and will not be point-to-point.
    auto interfaces =
        enumerateInterfaces(family, IFF_UP | IFF_MULTICAST, IFF_LOOPBACK | IFF_POINTOPOINT);
    return interfaces.size() > 0 ? interfaces[0] : "";
  } else if (network == ENVOY_NET_WLAN) {
    // Network is WiFi, so look for a cellular interface.
    // Cellular networks should be point-to-point.
    auto interfaces = enumerateInterfaces(family, IFF_UP | IFF_POINTOPOINT, IFF_LOOPBACK);
    return interfaces.size() > 0 ? interfaces[0] : "";
  } else {
    return "";
  }
}

std::vector<std::string>
Configurator::enumerateInterfaces([[maybe_unused]] unsigned short family,
                                  [[maybe_unused]] unsigned int select_flags,
                                  [[maybe_unused]] unsigned int reject_flags) {
  std::vector<std::string> names{};

#ifdef SUPPORTS_GETIFADDRS
  struct ifaddrs* interfaces = nullptr;
  struct ifaddrs* ifa = nullptr;

  const int rc = getifaddrs(&interfaces);
  RELEASE_ASSERT(!rc, "getifaddrs failed");

  for (ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != family) {
      continue;
    }
    if ((ifa->ifa_flags & (select_flags ^ reject_flags)) != select_flags) {
      continue;
    }
    names.push_back(std::string{ifa->ifa_name});
  }

  freeifaddrs(interfaces);
#endif // SUPPORTS_GETIFADDRS

  return names;
}

ConfiguratorSharedPtr ConfiguratorFactory::get() {
  return context_.singletonManager().getTyped<Configurator>(
      SINGLETON_MANAGER_REGISTERED_NAME(network_configurator), [&] {
        Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactoryImpl cache_manager_factory{
            context_};
        return std::make_shared<Configurator>(cache_manager_factory.get());
      });
}

ConfiguratorSharedPtr ConfiguratorHandle::get() {
  return singleton_manager_.getTyped<Configurator>(
      SINGLETON_MANAGER_REGISTERED_NAME(network_configurator));
}

} // namespace Network
} // namespace Envoy
