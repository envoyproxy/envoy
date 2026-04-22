#include "library/common/network/connectivity_manager.h"

#include <net/if.h>

#include <csetjmp>
#include <memory>

#include "envoy/common/platform.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/common/utility.h"
#include "source/common/network/addr_family_aware_socket_option_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"

#include "fmt/ostream.h"
#include "library/common/network/network_type_socket_option_impl.h"
#include "library/common/network/src_addr_socket_option_impl.h"
#include "library/common/system/system_helper.h"

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

ConnectivityManagerImpl::ConnectivityManagerImpl(Upstream::ClusterManager& cluster_manager,
                                                 DnsCacheManagerSharedPtr dns_cache_manager)
    : cluster_manager_(cluster_manager), quic_observer_registry_factory_(*this),
      dns_cache_manager_(dns_cache_manager) {
  initializeNetworkStates();
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.mobile_use_network_observer_registry")) {
    cluster_manager_.createNetworkObserverRegistries(quic_observer_registry_factory_);
  }
}

envoy_netconf_t ConnectivityManagerImpl::setPreferredNetwork(int network) {
  Thread::LockGuard lock{network_mutex_};

  // TODO(goaway): Re-enable this guard. There's some concern that this will miss network updates
  // moving from offline to online states. We should address this then re-enable this guard to
  // avoid unnecessary cache refresh and connection drain.
  // if (network_state_.network_ == network) {
  //  // Provide a non-current key preventing further scheduled effects (e.g. DNS refresh).
  //  return network_state_.configuration_key_ - 1;
  //}

  setPreferredNetworkNoLock(network);
  return network_state_.configuration_key_;
}

void ConnectivityManagerImpl::setPreferredNetworkNoLock(int network_type)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(network_mutex_) {
  ENVOY_LOG_EVENT(debug, "netconf_network_change", "network_type changed to {}",
                  std::to_string(static_cast<int>(network_type)));

  network_state_.configuration_key_++;
  network_state_.network_ = network_type;
  network_state_.remaining_faults_ = 1;
  network_state_.socket_mode_ = SocketMode::DefaultPreferredNetworkMode;
}

void ConnectivityManagerImpl::setProxySettings(ProxySettingsConstSharedPtr new_proxy_settings) {
  if (proxy_settings_ == nullptr && new_proxy_settings != nullptr) {
    ENVOY_LOG_EVENT(info, "netconf_proxy_change", "{}", new_proxy_settings->asString());
    proxy_settings_ = new_proxy_settings;
  } else if (proxy_settings_ != nullptr && new_proxy_settings == nullptr) {
    ENVOY_LOG_EVENT(info, "netconf_proxy_change", "no_proxy_configured");
    proxy_settings_ = new_proxy_settings;
  } else if (proxy_settings_ != nullptr && new_proxy_settings != nullptr &&
             *proxy_settings_ != *new_proxy_settings) {
    ENVOY_LOG_EVENT(info, "netconf_proxy_change", "{}", new_proxy_settings->asString());
    proxy_settings_ = new_proxy_settings;
  }
}

ProxySettingsConstSharedPtr ConnectivityManagerImpl::getProxySettings() { return proxy_settings_; }

int ConnectivityManagerImpl::getPreferredNetwork() {
  Thread::LockGuard lock{network_mutex_};
  return network_state_.network_;
}

SocketMode ConnectivityManagerImpl::getSocketMode() {
  Thread::LockGuard lock{network_mutex_};
  return network_state_.socket_mode_;
}

envoy_netconf_t ConnectivityManagerImpl::getConfigurationKey() {
  Thread::LockGuard lock{network_mutex_};
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
    Thread::LockGuard lock{network_mutex_};

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
        network_state_.socket_mode_ =
            network_state_.socket_mode_ == SocketMode::DefaultPreferredNetworkMode
                ? SocketMode::AlternateBoundInterfaceMode
                : SocketMode::DefaultPreferredNetworkMode;
        network_state_.remaining_faults_ = InitialFaultThreshold;
        if (network_state_.socket_mode_ == SocketMode::DefaultPreferredNetworkMode) {
          ENVOY_LOG_EVENT(debug, "netconf_mode_switch", "DefaultPreferredNetworkMode");
        } else if (network_state_.socket_mode_ == SocketMode::AlternateBoundInterfaceMode) {
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

void RefreshDnsWithPostDrainHandler::refreshDnsAndDrainHosts() {
  Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dns_cache =
      dns_cache_manager_->lookUpCacheByName(BaseDnsCache);
  if (!dns_cache) {
    // There may not be a DNS cache during initialization, but if one is available, it should always
    // exist by the time this handler is instantiated from the NetworkConfigurationFilter.
    ENVOY_LOG_EVENT(warn, "netconf_dns_cache_missing", "{}", std::string(BaseDnsCache));
    return;
  }
  if (dns_callbacks_handle_ == nullptr) {
    // Register callbacks once, on demand, using the handler as a sentinel.
    dns_callbacks_handle_ = dns_cache->addUpdateCallbacks(*this);
  }
  dns_cache->iterateHostMap(
      [&](absl::string_view host,
          const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr&) {
        hosts_to_drain_.emplace(host);
      });

  dns_cache->forceRefreshHosts();
}

void RefreshDnsWithPostDrainHandler::onDnsResolutionComplete(
    const std::string& resolved_host,
    const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr&,
    Network::DnsResolver::ResolutionStatus) {
  // Check if the set of hosts pending drain contains the current resolved host.
  if (hosts_to_drain_.erase(resolved_host) == 0) {
    return;
  }

  // We ignore whether DNS resolution has succeeded here. If it failed, we may be offline and
  // should probably drain connections. If it succeeds, we may have new DNS entries and so we
  // drain connections. It may be possible to refine this logic in the future.
  // TODO(goaway): check the set of cached hosts from the last triggered DNS refresh for this
  // host, and if present, remove it and trigger connection drain for this host specifically.
  ENVOY_LOG_EVENT(debug, "netconf_post_dns_drain_cx", "{}", resolved_host);

  // Pass predicate to only drain connections to the resolved host (for any cluster).
  cluster_manager_.drainConnections(
      [resolved_host](const Upstream::Host& host) { return host.hostname() == resolved_host; },
      ConnectionPool::DrainBehavior::DrainExistingConnections);
}

void ConnectivityManagerImpl::setDrainPostDnsRefreshEnabled(bool enabled) {
  if (!enabled) {
    dns_refresh_handler_ = nullptr;
  } else if (!dns_refresh_handler_) {
    dns_refresh_handler_ =
        std::make_unique<RefreshDnsWithPostDrainHandler>(dns_cache_manager_, cluster_manager_);
  }
}

void ConnectivityManagerImpl::setInterfaceBindingEnabled(bool enabled) {
  enable_interface_binding_ = enabled;
}

void ConnectivityManagerImpl::refreshDns(envoy_netconf_t configuration_key,
                                         bool drain_connections) {
  {
    Thread::LockGuard lock{network_mutex_};

    // refreshDns must be queued on Envoy's event loop, whereas network_state_ is updated
    // synchronously. In the event that multiple refreshes become queued on the event loop,
    // this check avoids triggering a refresh for a non-current network.
    // Note this does NOT completely prevent parallel refreshes from being triggered in multiple
    // flip-flop scenarios.
    if (configuration_key != network_state_.configuration_key_) {
      ENVOY_LOG_EVENT(debug, "netconf_dns_flipflop", "{}", std::to_string(configuration_key));
      return;
    }
  }
  doRefreshDns(configuration_key, drain_connections);
}

void ConnectivityManagerImpl::doRefreshDns(envoy_netconf_t configuration_key,
                                           bool drain_connections) {
  if (auto dns_cache = dnsCache()) {
    ENVOY_LOG_EVENT(debug, "netconf_refresh_dns", "{}", std::to_string(configuration_key));

    if (drain_connections && (dns_refresh_handler_ != nullptr)) {
      dns_refresh_handler_->refreshDnsAndDrainHosts();
    } else {
      dns_cache->forceRefreshHosts();
    }
  }
}

Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr ConnectivityManagerImpl::dnsCache() {
  auto cache = dns_cache_manager_->lookUpCacheByName(BaseDnsCache);
  if (!cache) {
    ENVOY_LOG_EVENT(warn, "netconf_dns_cache_missing", "{}", std::string(BaseDnsCache));
  }
  return cache;
}

void ConnectivityManagerImpl::resetConnectivityState() {
  envoy_netconf_t configuration_key;
  {
    Thread::LockGuard lock{network_mutex_};
    network_state_.network_ = 0;
    network_state_.remaining_faults_ = 1;
    network_state_.socket_mode_ = SocketMode::DefaultPreferredNetworkMode;
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

Socket::OptionsSharedPtr ConnectivityManagerImpl::getUpstreamSocketOptions(int network,
                                                                           SocketMode socket_mode) {
  if (enable_interface_binding_ && socket_mode == SocketMode::AlternateBoundInterfaceMode &&
      network != 0) {
    return getAlternateInterfaceSocketOptions(network);
  }

  auto options = std::make_shared<Socket::Options>();
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.decouple_explicit_drain_pools_and_dns_refresh")) {
    // Envoy uses the hash signature of overridden socket options to choose a connection pool.
    // Setting a dummy socket option is a hack that allows us to select a different
    // connection pool without materially changing the socket configuration when
    // pools are not explicitly drained during network change.
    options->push_back(std::make_shared<NetworkTypeSocketOptionImpl>(network));
  }

  return options;
}

Socket::OptionsSharedPtr ConnectivityManagerImpl::getAlternateInterfaceSocketOptions(int network) {
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
  int network;
  SocketMode socket_mode;

  {
    Thread::LockGuard lock{network_mutex_};
    configuration_key = network_state_.configuration_key_;
    network = network_state_.network_;
    socket_mode = network_state_.socket_mode_;
  }

  auto new_options = getUpstreamSocketOptions(network, socket_mode);
  options->insert(options->end(), new_options->begin(), new_options->end());
  return configuration_key;
}

InterfacePair ConnectivityManagerImpl::getActiveAlternateInterface(int network,
                                                                   unsigned short family) {
  // Attempt to derive an active interface that differs from the passed network parameter.
  if (network & static_cast<int>(NetworkType::WWAN)) {
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
  } else if (network & static_cast<int>(NetworkType::WLAN)) {
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

// TODO(abeyad): pass OsSysCallsImpl in as a class dependency instead of directly using
// Api::OsSysCallsSingleton. That'll make it easier to create tests that mock out the
// sys calls behavior.
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
  if (rc.return_value_ != 0) {
    ENVOY_LOG_EVERY_POW_2(warn, "getifaddrs error: {}", rc.errno_);
    return pairs;
  }

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

int connectionTypeToCompoundNetworkType(ConnectionType connection_type) {
  int compound_type = 0;
  switch (connection_type) {
  case ConnectionType::CONNECTION_2G:
    compound_type |= (static_cast<int>(NetworkType::WWAN) | static_cast<int>(NetworkType::WWAN_2G));
    break;
  case ConnectionType::CONNECTION_3G:
    compound_type |= (static_cast<int>(NetworkType::WWAN) | static_cast<int>(NetworkType::WWAN_3G));
    break;
  case ConnectionType::CONNECTION_4G:
    compound_type |= (static_cast<int>(NetworkType::WWAN) | static_cast<int>(NetworkType::WWAN_4G));
    break;
  case ConnectionType::CONNECTION_5G:
    compound_type |= (static_cast<int>(NetworkType::WWAN) | static_cast<int>(NetworkType::WWAN_5G));
    break;
  case ConnectionType::CONNECTION_WIFI:
  case ConnectionType::CONNECTION_ETHERNET:
    compound_type |= static_cast<int>(NetworkType::WLAN);
    break;
  case ConnectionType::CONNECTION_NONE:
    break;
  case ConnectionType::CONNECTION_BLUETOOTH:
  case ConnectionType::CONNECTION_UNKNOWN:
    compound_type = static_cast<int>(NetworkType::Generic);
    break;
  }
  return compound_type;
}

void ConnectivityManagerImpl::onDefaultNetworkChangedAndroid(ConnectionType connection_type,
                                                             NetworkHandle net_id) {
  bool already_connected{false};
  envoy_netconf_t current_configuration_key{0};
  {
    Thread::LockGuard lock{network_mutex_};
    ENVOY_LOG_EVENT(debug, "android_default_network_changed",
                    "default network changed from {} to {}, new connection_type {}, ",
                    default_network_handle_, net_id, static_cast<int>(connection_type));
    if (net_id == default_network_handle_) {
      return;
    }
    current_configuration_key = network_state_.configuration_key_;
    default_network_handle_ = net_id;
    if (connected_networks_.find(net_id) != connected_networks_.end()) {
      // Android Lollipop had race conditions where CONNECTIVITY_ACTION intents
      // were sent out before the network was actually made the default.
      // Delay switching to the new default until Android platform notifies that the network
      // connected.
      setPreferredNetworkNoLock(connectionTypeToCompoundNetworkType(connection_type));
      current_configuration_key = network_state_.configuration_key_;
      already_connected = true;
    }
  }
  if (already_connected) {
    if (default_network_change_callback_ != nullptr) {
      default_network_change_callback_(current_configuration_key);
    }
    for (std::reference_wrapper<Quic::EnvoyMobileQuicNetworkObserverRegistry> registry :
         quic_observer_registry_factory_.getCreatedObserverRegistries()) {
      registry.get().onNetworkMadeDefault(net_id);
    }
  }
}

void ConnectivityManagerImpl::onNetworkDisconnectAndroid(NetworkHandle net_id) {
  {
    Thread::LockGuard lock{network_mutex_};
    if (net_id == default_network_handle_) {
      default_network_handle_ = kInvalidNetworkHandle;
    }
    if (connected_networks_.erase(net_id) == 0) {
      return;
    }
  }
  for (std::reference_wrapper<Quic::EnvoyMobileQuicNetworkObserverRegistry> registry :
       quic_observer_registry_factory_.getCreatedObserverRegistries()) {
    registry.get().onNetworkDisconnected(net_id);
  }
}

void ConnectivityManagerImpl::onNetworkConnectAndroid(ConnectionType connection_type,
                                                      NetworkHandle net_id) {
  bool is_default_network{false};
  envoy_netconf_t current_configuration_key{0};
  {
    Thread::LockGuard lock{network_mutex_};
    if (connected_networks_.find(net_id) != connected_networks_.end()) {
      return;
    }
    connected_networks_[net_id] = connection_type;
    current_configuration_key = network_state_.configuration_key_;
    if (net_id == default_network_handle_) {
      // The reported default network finally gets connected.
      is_default_network = true;
      setPreferredNetworkNoLock(connectionTypeToCompoundNetworkType(connection_type));
      current_configuration_key = network_state_.configuration_key_;
    }
  }
  if (is_default_network) {
    if (default_network_change_callback_ != nullptr) {
      default_network_change_callback_(current_configuration_key);
    }
  }

  // Android Lollipop would send many duplicate notifications.
  // This was later fixed in Android Marshmallow.
  // Deduplicate them here by avoiding sending duplicate notifications.
  for (std::reference_wrapper<Quic::EnvoyMobileQuicNetworkObserverRegistry> registry :
       quic_observer_registry_factory_.getCreatedObserverRegistries()) {
    registry.get().onNetworkConnected(net_id);
    if (is_default_network) {
      registry.get().onNetworkMadeDefault(net_id);
    }
  }
}

void ConnectivityManagerImpl::purgeActiveNetworkListAndroid(
    const std::vector<NetworkHandle>& active_network_ids) {
  std::vector<int64_t> disconnected_networks;
  {
    Thread::LockGuard lock{network_mutex_};
    for (auto& i : connected_networks_) {
      if (std::find(active_network_ids.begin(), active_network_ids.end(), i.first) ==
          active_network_ids.end()) {
        disconnected_networks.push_back(i.first);
      }
    }
  }
  for (auto disconnected_network : disconnected_networks) {
    onNetworkDisconnectAndroid(disconnected_network);
  }
}

void ConnectivityManagerImpl::initializeNetworkStates() {
  NetworkHandle default_net_id = SystemHelper::getInstance().getDefaultNetworkHandle();
  std::vector<std::pair<int64_t, ConnectionType>> all_connected_networks =
      SystemHelper::getInstance().getAllConnectedNetworks();

  Thread::LockGuard lock{network_mutex_};
  default_network_handle_ = default_net_id;
  for (auto& entry : all_connected_networks) {
    connected_networks_[entry.first] = entry.second;
  }
}

NetworkHandle ConnectivityManagerImpl::getDefaultNetwork() {
  Thread::LockGuard lock{network_mutex_};
  return default_network_handle_;
}

absl::flat_hash_map<NetworkHandle, ConnectionType>
ConnectivityManagerImpl::getAllConnectedNetworks() {
  Thread::LockGuard lock{network_mutex_};
  return connected_networks_;
}

ConnectivityManagerImplSharedPtr ConnectivityManagerFactory::get() {
  return context_.serverFactoryContext().singletonManager().getTyped<ConnectivityManagerImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(connectivity_manager), [this] {
        Envoy::Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactoryImpl
            cache_manager_factory{context_};
        return std::make_shared<ConnectivityManagerImpl>(
            context_.serverFactoryContext().clusterManager(), cache_manager_factory.get());
      });
}

ConnectivityManagerSharedPtr ConnectivityManagerHandle::get() {
  return singleton_manager_.getTyped<ConnectivityManagerImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(connectivity_manager));
}

} // namespace Network
} // namespace Envoy
