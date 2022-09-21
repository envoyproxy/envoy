#include "library/common/network/connectivity_manager.h"

#include <net/if.h>

#include "envoy/common/platform.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/common/utility.h"
#include "source/common/network/addr_family_aware_socket_option_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"

#include "fmt/ostream.h"
#include "library/common/network/src_addr_socket_option_impl.h"

// Used on Linux (requires root/CAP_NET_RAW)
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

// Prefixes used to prefer well-known interface names.
#if defined(__APPLE__)
constexpr absl::string_view WlanPrefix = "en";
constexpr absl::string_view WwanPrefix = "pdp_ip";
#elif defined(__ANDROID_API__)
constexpr absl::string_view WlanPrefix = "wlan";
constexpr absl::string_view WwanPrefix = "rmnet";
#else
// An empty prefix is essentially the same as disabling filtering since it will always match.
constexpr absl::string_view WlanPrefix = "";
constexpr absl::string_view WwanPrefix = "";
#endif

namespace Envoy {
namespace Network {

SINGLETON_MANAGER_REGISTRATION(connectivity_manager);

constexpr absl::string_view BaseDnsCache = "base_dns_cache";

// The number of faults allowed on a newly-established connection before switching socket mode.
constexpr unsigned int InitialFaultThreshold = 1;
// The number of faults allowed on a previously-successful connection (i.e. able to send and receive
// L7 bytes) before switching socket mode.
constexpr unsigned int MaxFaultThreshold = 3;

ConnectivityManagerImpl::NetworkState ConnectivityManagerImpl::network_state_{
    1, ENVOY_NET_GENERIC, MaxFaultThreshold, DefaultPreferredNetworkMode,
    Thread::MutexBasicLockable{}};

envoy_netconf_t ConnectivityManagerImpl::setPreferredNetwork(envoy_network_t network) {
  Thread::LockGuard lock{network_state_.mutex_};

  // TODO(goaway): Re-enable this guard. There's some concern that this will miss network updates
  // moving from offline to online states. We should address this then re-enable this guard to
  // avoid unnecessary cache refresh and connection drain.
  // if (network_state_.network_ == network) {
  //  // Provide a non-current key preventing further scheduled effects (e.g. DNS refresh).
  //  return network_state_.configuration_key_ - 1;
  //}

  ENVOY_LOG_EVENT(debug, "netconf_network_change", std::to_string(network));

  network_state_.configuration_key_++;
  network_state_.network_ = network;
  network_state_.remaining_faults_ = 1;
  network_state_.socket_mode_ = DefaultPreferredNetworkMode;

  return network_state_.configuration_key_;
}

void ConnectivityManagerImpl::setProxySettings(ProxySettingsConstSharedPtr new_proxy_settings) {
  if (proxy_settings_ == nullptr && new_proxy_settings != nullptr) {
    ENVOY_LOG_EVENT(info, "netconf_proxy_change", new_proxy_settings->asString());
    proxy_settings_ = new_proxy_settings;
  } else if (proxy_settings_ != nullptr && new_proxy_settings == nullptr) {
    ENVOY_LOG_EVENT(info, "netconf_proxy_change", "no_proxy_configured");
    proxy_settings_ = new_proxy_settings;
  } else if (proxy_settings_ != nullptr && new_proxy_settings != nullptr &&
             *proxy_settings_ != *new_proxy_settings) {
    ENVOY_LOG_EVENT(info, "netconf_proxy_change", new_proxy_settings->asString());
    proxy_settings_ = new_proxy_settings;
  }

  return;
}

ProxySettingsConstSharedPtr ConnectivityManagerImpl::getProxySettings() { return proxy_settings_; }

envoy_network_t ConnectivityManagerImpl::getPreferredNetwork() {
  Thread::LockGuard lock{network_state_.mutex_};
  return network_state_.network_;
}

envoy_socket_mode_t ConnectivityManagerImpl::getSocketMode() {
  Thread::LockGuard lock{network_state_.mutex_};
  return network_state_.socket_mode_;
}

envoy_netconf_t ConnectivityManagerImpl::getConfigurationKey() {
  Thread::LockGuard lock{network_state_.mutex_};
  return network_state_.configuration_key_;
}

// This call contains the main heuristic that will determine if the network connectivity_manager
// switches socket modes: If the configuration_key isn't current, don't do anything. If there was no
// fault (i.e. success) reset remaining_faults_ to MaxFaultTreshold. If there was a network fault,
// decrement remaining_faults_.
//   - At 0, increment configuration_key, reset remaining_faults_ to InitialFaultThreshold and
//     toggle socket_mode_.
void ConnectivityManagerImpl::reportNetworkUsage(envoy_netconf_t configuration_key,
                                                 bool network_fault) {
  ENVOY_LOG(debug, "reportNetworkUsage(configuration_key: {}, network_fault: {})",
            configuration_key, network_fault);

  if (!enable_interface_binding_) {
    ENVOY_LOG(debug, "bailing due to interface binding being disabled");
    return;
  }

  bool configuration_updated = false;
  {
    Thread::LockGuard lock{network_state_.mutex_};

    // If the configuration_key isn't current, don't do anything.
    if (configuration_key != network_state_.configuration_key_) {
      ENVOY_LOG(debug, "bailing due to stale configuration key");
      return;
    }

    if (!network_fault) {
      // If there was no fault (i.e. success) reset remaining_faults_ to MaxFaultThreshold.
      ENVOY_LOG(debug, "resetting fault threshold");
      network_state_.remaining_faults_ = MaxFaultThreshold;
    } else {
      // If there was a network fault, decrement remaining_faults_.
      ASSERT(network_state_.remaining_faults_ > 0);
      network_state_.remaining_faults_--;
      ENVOY_LOG(debug, "decrementing remaining faults; {} remaining",
                network_state_.remaining_faults_);

      // At 0, increment configuration_key, reset remaining_faults_ to InitialFaultThreshold and
      // toggle socket_mode_.
      if (network_state_.remaining_faults_ == 0) {
        configuration_updated = true;
        configuration_key = ++network_state_.configuration_key_;
        network_state_.socket_mode_ = network_state_.socket_mode_ == DefaultPreferredNetworkMode
                                          ? AlternateBoundInterfaceMode
                                          : DefaultPreferredNetworkMode;
        network_state_.remaining_faults_ = InitialFaultThreshold;
        if (network_state_.socket_mode_ == DefaultPreferredNetworkMode) {
          ENVOY_LOG_EVENT(debug, "netconf_mode_switch", "DefaultPreferredNetworkMode");
        } else if (network_state_.socket_mode_ == AlternateBoundInterfaceMode) {
          auto v4_pair = getActiveAlternateInterface(network_state_.network_, AF_INET);
          auto v6_pair = getActiveAlternateInterface(network_state_.network_, AF_INET6);
          ENVOY_LOG_EVENT(debug, "netconf_mode_switch", "AlternateBoundInterfaceMode [{}|{}]",
                          std::get<const std::string>(v4_pair),
                          std::get<const std::string>(v6_pair));
        }
      }
    }
  }

  // If configuration state changed, refresh dns.
  if (configuration_updated) {
    refreshDns(configuration_key, false);
  }
}

void ConnectivityManagerImpl::onDnsResolutionComplete(
    const std::string& resolved_host,
    const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr&,
    Network::DnsResolver::ResolutionStatus) {
  if (enable_drain_post_dns_refresh_) {
    // Check if the set of hosts pending drain contains the current resolved host.
    if (hosts_to_drain_.erase(resolved_host) == 0) {
      return;
    }

    // We ignore whether DNS resolution has succeeded here. If it failed, we may be offline and
    // should probably drain connections. If it succeeds, we may have new DNS entries and so we
    // drain connections. It may be possible to refine this logic in the future.
    // TODO(goaway): check the set of cached hosts from the last triggered DNS refresh for this
    // host, and if present, remove it and trigger connection drain for this host specifically.
    ENVOY_LOG_EVENT(debug, "netconf_post_dns_drain_cx", resolved_host);

    // Pass predicate to only drain connections to the resolved host (for any cluster).
    cluster_manager_.drainConnections(
        [resolved_host](const Upstream::Host& host) { return host.hostname() == resolved_host; });
  }
}

void ConnectivityManagerImpl::setDrainPostDnsRefreshEnabled(bool enabled) {
  enable_drain_post_dns_refresh_ = enabled;
  if (!enabled) {
    hosts_to_drain_.clear();
  } else if (!dns_callbacks_handle_) {
    // Register callbacks once, on demand, using the handle as a sentinel. There may not be
    // a DNS cache during initialization, but if one is available, it should always exist by the
    // time this function is called from the NetworkConfigurationFilter.
    if (auto dns_cache = dnsCache()) {
      dns_callbacks_handle_ = dns_cache->addUpdateCallbacks(*this);
    }
  }
}

void ConnectivityManagerImpl::setInterfaceBindingEnabled(bool enabled) {
  enable_interface_binding_ = enabled;
}

void ConnectivityManagerImpl::refreshDns(envoy_netconf_t configuration_key,
                                         bool drain_connections) {
  {
    Thread::LockGuard lock{network_state_.mutex_};

    // refreshDns must be queued on Envoy's event loop, whereas network_state_ is updated
    // synchronously. In the event that multiple refreshes become queued on the event loop,
    // this check avoids triggering a refresh for a non-current network.
    // Note this does NOT completely prevent parallel refreshes from being triggered in multiple
    // flip-flop scenarios.
    if (configuration_key != network_state_.configuration_key_) {
      ENVOY_LOG_EVENT(debug, "netconf_dns_flipflop", std::to_string(configuration_key));
      return;
    }
  }

  if (auto dns_cache = dnsCache()) {
    ENVOY_LOG_EVENT(debug, "netconf_refresh_dns", std::to_string(configuration_key));

    if (drain_connections && enable_drain_post_dns_refresh_) {
      dns_cache->iterateHostMap(
          [&](absl::string_view host,
              const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr&) {
            hosts_to_drain_.emplace(host);
          });
    }

    dns_cache->forceRefreshHosts();
  }
}

Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr ConnectivityManagerImpl::dnsCache() {
  auto cache = dns_cache_manager_->lookUpCacheByName(BaseDnsCache);
  if (!cache) {
    ENVOY_LOG_EVENT(warn, "netconf_dns_cache_missing", BaseDnsCache);
  }
  return cache;
}

void ConnectivityManagerImpl::resetConnectivityState() {
  envoy_netconf_t configuration_key;
  {
    Thread::LockGuard lock{network_state_.mutex_};
    network_state_.remaining_faults_ = 1;
    network_state_.socket_mode_ = DefaultPreferredNetworkMode;
    configuration_key = ++network_state_.configuration_key_;
  }

  refreshDns(configuration_key, true);
}

std::vector<InterfacePair> ConnectivityManagerImpl::enumerateV4Interfaces() {
  return enumerateInterfaces(AF_INET, 0, 0);
}

std::vector<InterfacePair> ConnectivityManagerImpl::enumerateV6Interfaces() {
  return enumerateInterfaces(AF_INET6, 0, 0);
}

Socket::OptionsSharedPtr
ConnectivityManagerImpl::getUpstreamSocketOptions(envoy_network_t network,
                                                  envoy_socket_mode_t socket_mode) {
  if (enable_interface_binding_ && socket_mode == AlternateBoundInterfaceMode &&
      network != ENVOY_NET_GENERIC) {
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

Socket::OptionsSharedPtr
ConnectivityManagerImpl::getAlternateInterfaceSocketOptions(envoy_network_t network) {
  auto v4_pair = getActiveAlternateInterface(network, AF_INET);
  auto v6_pair = getActiveAlternateInterface(network, AF_INET6);
  ENVOY_LOG(debug, "found active alternate interface (ipv4): {} {}", std::get<0>(v4_pair),
            std::get<1>(v4_pair)->asString());
  ENVOY_LOG(debug, "found active alternate interface (ipv6): {} {}", std::get<0>(v6_pair),
            std::get<1>(v6_pair)->asString());

  auto options = std::make_shared<Socket::Options>();

#ifdef IP_BOUND_IF
  // iOS
  // On platforms where it exists, IP_BOUND_IF/IPV6_BOUND_IF provide a straightforward way to bind
  // a socket explicitly to specific interface. (The Linux alternative is SO_BINDTODEVICE, but has
  // other restriction; see below.)
  int v4_idx = if_nametoindex(std::get<const std::string>(v4_pair).c_str());
  int v6_idx = if_nametoindex(std::get<const std::string>(v6_pair).c_str());
  options->push_back(std::make_shared<AddrFamilyAwareSocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_PREBIND, ENVOY_SOCKET_IP_BOUND_IF, v4_idx,
      ENVOY_SOCKET_IPV6_BOUND_IF, v6_idx));
#else
  // Android
  // SO_BINDTODEVICE is defined on Android, but applying it requires root privileges (or more
  // specifically, CAP_NET_RAW). As a workaround, this binds the socket to the interface by
  // attaching "synthetic" socket option, which sets the socket's source address to the local
  // address of the interface. This is not quite as precise, since it's possible that multiple
  // interfaces share the same local address, but this is all best-effort anyways.
  options->push_back(std::make_shared<AddrFamilyAwareSocketOptionImpl>(
      std::make_unique<SrcAddrSocketOptionImpl>(std::get<1>(v4_pair)),
      std::make_unique<SrcAddrSocketOptionImpl>(std::get<1>(v6_pair))));
#endif

  return options;
}

envoy_netconf_t
ConnectivityManagerImpl::addUpstreamSocketOptions(Socket::OptionsSharedPtr options) {
  envoy_netconf_t configuration_key;
  envoy_network_t network;
  envoy_socket_mode_t socket_mode;

  {
    Thread::LockGuard lock{network_state_.mutex_};
    configuration_key = network_state_.configuration_key_;
    network = network_state_.network_;
    socket_mode = network_state_.socket_mode_;
  }

  auto new_options = getUpstreamSocketOptions(network, socket_mode);
  options->insert(options->end(), new_options->begin(), new_options->end());
  return configuration_key;
}

InterfacePair ConnectivityManagerImpl::getActiveAlternateInterface(envoy_network_t network,
                                                                   unsigned short family) {
  // Attempt to derive an active interface that differs from the passed network parameter.
  if (network == ENVOY_NET_WWAN) {
    // Network is cellular, so look for a WiFi interface.
    // WiFi should always support multicast, and will not be point-to-point.
    auto interfaces =
        enumerateInterfaces(family, IFF_UP | IFF_MULTICAST, IFF_LOOPBACK | IFF_POINTOPOINT);
    for (const auto& interface : interfaces) {
      // Look for interface with name that matches the expected prefix.
      // TODO(goaway): This is quite brittle. It would be an improvement to:
      //   1) Improve the scoping via flags.
      //   2) Prioritize interfaces by prefix instead of simply filtering them.
      if (absl::StartsWith(std::get<const std::string>(interface), WlanPrefix)) {
        return interface;
      }
    }
  } else if (network == ENVOY_NET_WLAN) {
    // Network is WiFi, so look for a cellular interface.
    // Cellular networks should be point-to-point.
    auto interfaces = enumerateInterfaces(family, IFF_UP | IFF_POINTOPOINT, IFF_LOOPBACK);
    for (const auto& interface : interfaces) {
      // Look for interface with name that matches the expected prefix.
      // TODO(goaway): This is quite brittle. It would be an improvement to:
      //   1) Improve the scoping via flags.
      //   2) Prioritize interfaces by prefix instead of simply filtering them.
      if (absl::StartsWith(std::get<const std::string>(interface), WwanPrefix)) {
        return interface;
      }
    }
  }

  return std::make_pair("", nullptr);
}

std::vector<InterfacePair>
ConnectivityManagerImpl::enumerateInterfaces([[maybe_unused]] unsigned short family,
                                             [[maybe_unused]] unsigned int select_flags,
                                             [[maybe_unused]] unsigned int reject_flags) {
  std::vector<InterfacePair> pairs{};

  if (!Api::OsSysCallsSingleton::get().supportsGetifaddrs()) {
    return pairs;
  }

  Api::InterfaceAddressVector interface_addresses{};
  const Api::SysCallIntResult rc = Api::OsSysCallsSingleton::get().getifaddrs(interface_addresses);
  RELEASE_ASSERT(!rc.return_value_, fmt::format("getiffaddrs error: {}", rc.errno_));

  for (const auto& interface_address : interface_addresses) {
    const auto family_version = family == AF_INET ? Envoy::Network::Address::IpVersion::v4
                                                  : Envoy::Network::Address::IpVersion::v6;
    if (interface_address.interface_addr_->ip()->version() != family_version) {
      continue;
    }

    if ((interface_address.interface_flags_ & (select_flags ^ reject_flags)) != select_flags) {
      continue;
    }

    pairs.push_back(
        std::make_pair(interface_address.interface_name_, interface_address.interface_addr_));
  }

  return pairs;
}

ConnectivityManagerSharedPtr ConnectivityManagerFactory::get() {
  return context_.singletonManager().getTyped<ConnectivityManagerImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(connectivity_manager), [&] {
        Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactoryImpl cache_manager_factory{
            context_};
        return std::make_shared<ConnectivityManagerImpl>(context_.clusterManager(),
                                                         cache_manager_factory.get());
      });
}

ConnectivityManagerSharedPtr ConnectivityManagerHandle::get() {
  return singleton_manager_.getTyped<ConnectivityManagerImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(connectivity_manager));
}

} // namespace Network
} // namespace Envoy
