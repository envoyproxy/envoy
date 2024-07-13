#pragma once

#include <string>
#include <vector>

#include "envoy/network/socket.h"
#include "envoy/singleton/manager.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/extensions/common/dynamic_forward_proxy/dns_cache.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_impl.h"

#include "library/common/engine_types.h"
#include "library/common/network/proxy_settings.h"
#include "library/common/types/c_types.h"

/**
 * envoy_netconf_t identifies a snapshot of network configuration state. It's returned from calls
 * that may alter current state, and passed back as a parameter to this API to determine if calls
 * remain valid/relevant at time of execution.
 *
 * Currently, there are two primary circumstances this is used:
 * 1. When network type changes, a refreshDNS call will be scheduled on the event dispatcher, along
 * with a configuration key of this type. If network type changes again before that refresh
 * executes, the refresh is now stale, another refresh task will have been queued, and it should no
 * longer execute. The configuration key allows the connectivity_manager to determine if the
 * refreshDNS call is representative of current configuration.
 * 2. When a request is configured with a certain set of socket options and begins, it is given a
 * configuration key. The heuristic in reportNetworkUsage relies on characteristics of the
 * request/response to make future decisions about socket options, but needs to be able to correctly
 * associate these metrics with their original configuration. If network state changes while the
 * request/response are in-flight, the connectivity_manager can determine the relevance of
 * associated metrics through the configuration key.
 *
 * Additionally, in the future, more advanced heuristics may maintain multiple parallel
 * configurations across different interfaces/network types. In these more complicated scenarios, a
 * configuration key will be able to identify not only if the configuration is current, but also
 * which of several current configurations is relevant.
 */
typedef uint16_t envoy_netconf_t;

namespace Envoy {
namespace Network {

/**
 * These values specify the behavior of the network connectivity_manager with respect to the
 * upstream socket options it supplies.
 */
enum class SocketMode : int {
  // In this mode, the connectivity_manager will provide socket options that result in the creation
  // of a
  // distinct connection pool for a given value of preferred network.
  DefaultPreferredNetworkMode = 0,
  // In this mode, the connectivity_manager will provide socket options that intentionally attempt
  // to
  // override the current preferred network type with an alternative, via interface-binding socket
  // options. Note this mode is experimental, and it will not be enabled at all unless
  // enable_interface_binding_ is set to true.
  AlternateBoundInterfaceMode = 1,
};

using DnsCacheManagerSharedPtr = Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr;
using InterfacePair = std::pair<const std::string, Address::InstanceConstSharedPtr>;

/**
 * Object responsible for tracking network state, especially with respect to multiple interfaces,
 * and providing auxiliary configuration to network connections, in the form of upstream socket
 * options.
 *
 * Code is largely structured to be run exclusively on the engine's main thread. However,
 * setPreferredNetwork is allowed to be called from any thread, and the internal NetworkState that
 * it modifies owns a mutex used to synchronize all access to that state.
 * (Note NetworkState was originally designed to fit into an atomic, and could still feasibly be
 * switched to one.)
 *
 * This object is a singleton per-engine. Note that several pieces of functionality assume a DNS
 * cache adhering to the one set up in base configuration will be present, but will become no-ops
 * if that cache is missing either due to alternate configurations, or lifecycle-related timing.
 *
 */
class ConnectivityManager
    : public Extensions::Common::DynamicForwardProxy::DnsCache::UpdateCallbacks {
public:
  virtual ~ConnectivityManager() = default;

  /**
   * @returns a list of local network interfaces supporting IPv4.
   */
  virtual std::vector<InterfacePair> enumerateV4Interfaces() PURE;

  /**
   * @returns a list of local network interfaces supporting IPv6.
   */
  virtual std::vector<InterfacePair> enumerateV6Interfaces() PURE;

  /**
   * @param family, network family of the interface.
   * @param select_flags, flags which MUST be set for each returned interface.
   * @param reject_flags, flags which MUST NOT be set for any returned interface.
   * @returns a list of local network interfaces filtered by the providered flags.
   */
  virtual std::vector<InterfacePair> enumerateInterfaces(unsigned short family,
                                                         unsigned int select_flags,
                                                         unsigned int reject_flags) PURE;

  /**
   * @returns the current OS default/preferred network class.
   */
  virtual NetworkType getPreferredNetwork() PURE;

  /**
   * @returns the current mode used to determine upstream socket options.
   */
  virtual SocketMode getSocketMode() PURE;

  /**
   * @returns configuration key representing current network state.
   */
  virtual envoy_netconf_t getConfigurationKey() PURE;

  /**
   *
   * @return the current proxy settings.
   */
  virtual Envoy::Network::ProxySettingsConstSharedPtr getProxySettings() PURE;

  /**
   * Call to report on the current viability of the passed network configuration after an attempt
   * at transmission (e.g., an HTTP request).
   * @param network_fault, whether a transmission attempt terminated w/o receiving upstream bytes.
   */
  virtual void reportNetworkUsage(envoy_netconf_t configuration_key, bool network_fault) PURE;

  /**
   * @brief Sets the current proxy settings.
   *
   * @param proxy_settings The proxy settings. `nullptr` if there is no proxy configured on a
   * device.
   */
  virtual void setProxySettings(ProxySettingsConstSharedPtr proxy_settings) PURE;

  /**
   * Configure whether connections should be drained after a triggered DNS refresh. Currently this
   * may happen either due to an external call to refreshConnectivityState or an update to
   * setPreferredNetwork.
   * @param enabled, whether to enable connection drain after DNS refresh.
   */
  virtual void setDrainPostDnsRefreshEnabled(bool enabled) PURE;

  /**
   * Sets whether subsequent calls for upstream socket options may leverage options that bind
   * to specific network interfaces.
   * @param enabled, whether to enable interface binding.
   */
  virtual void setInterfaceBindingEnabled(bool enabled) PURE;

  /**
   * Refresh DNS in response to preferred network update. May be no-op.
   * @param configuration_key, key provided by this class representing the current configuration.
   * @param drain_connections, request that connections be drained after next DNS resolution.
   */
  virtual void refreshDns(envoy_netconf_t configuration_key, bool drain_connections) PURE;

  /**
   * Drain all upstream connections associated with this Engine.
   */
  virtual void resetConnectivityState() PURE;

  /**
   * @returns the current socket options that should be used for connections.
   */
  virtual Socket::OptionsSharedPtr getUpstreamSocketOptions(NetworkType network,
                                                            SocketMode socket_mode) PURE;

  /**
   * @param options, upstream connection options to which additional options should be appended.
   * @returns configuration key to associate with any related calls.
   */
  virtual envoy_netconf_t addUpstreamSocketOptions(Socket::OptionsSharedPtr options) PURE;

  /**
   * Returns the default DNS cache set up in base configuration. This cache may be missing either
   * due to engine lifecycle-related timing or alternate configurations. If it is, operations
   * that use it should revert to no-ops.
   *
   * @returns the default DNS cache set up in base configuration or nullptr.
   */
  virtual Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dnsCache() PURE;
};

class ConnectivityManagerImpl : public ConnectivityManager,
                                public Singleton::Instance,
                                public Logger::Loggable<Logger::Id::upstream> {
public:
  /**
   * Sets the current OS default/preferred network class. Note this function is allowed to be
   * called from any thread.
   * @param network, the OS-preferred network.
   * @returns configuration key to associate with any related calls.
   */
  static envoy_netconf_t setPreferredNetwork(NetworkType network);

  ConnectivityManagerImpl(Upstream::ClusterManager& cluster_manager,
                          DnsCacheManagerSharedPtr dns_cache_manager)
      : cluster_manager_(cluster_manager), dns_cache_manager_(dns_cache_manager) {}

  // Extensions::Common::DynamicForwardProxy::DnsCache::UpdateCallbacks
  void onDnsHostAddOrUpdate(
      const std::string& /*host*/,
      const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr&) override {}
  void onDnsHostRemove(const std::string& /*host*/) override {}
  void onDnsResolutionComplete(const std::string& /*host*/,
                               const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr&,
                               Network::DnsResolver::ResolutionStatus) override;

  // ConnectivityManager
  std::vector<InterfacePair> enumerateV4Interfaces() override;
  std::vector<InterfacePair> enumerateV6Interfaces() override;
  std::vector<InterfacePair> enumerateInterfaces(unsigned short family, unsigned int select_flags,
                                                 unsigned int reject_flags) override;
  NetworkType getPreferredNetwork() override;
  SocketMode getSocketMode() override;
  envoy_netconf_t getConfigurationKey() override;
  Envoy::Network::ProxySettingsConstSharedPtr getProxySettings() override;
  void reportNetworkUsage(envoy_netconf_t configuration_key, bool network_fault) override;
  void setProxySettings(ProxySettingsConstSharedPtr new_proxy_settings) override;
  void setDrainPostDnsRefreshEnabled(bool enabled) override;
  void setInterfaceBindingEnabled(bool enabled) override;
  void refreshDns(envoy_netconf_t configuration_key, bool drain_connections) override;
  void resetConnectivityState() override;
  Socket::OptionsSharedPtr getUpstreamSocketOptions(NetworkType network,
                                                    SocketMode socket_mode) override;
  envoy_netconf_t addUpstreamSocketOptions(Socket::OptionsSharedPtr options) override;
  Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dnsCache() override;

private:
  struct NetworkState {
    // The configuration key is passed through calls dispatched on the run loop to determine if
    // they're still valid/relevant at time of execution.
    envoy_netconf_t configuration_key_ ABSL_GUARDED_BY(mutex_);
    NetworkType network_ ABSL_GUARDED_BY(mutex_);
    uint8_t remaining_faults_ ABSL_GUARDED_BY(mutex_);
    SocketMode socket_mode_ ABSL_GUARDED_BY(mutex_);
    Thread::MutexBasicLockable mutex_;
  };
  Socket::OptionsSharedPtr getAlternateInterfaceSocketOptions(NetworkType network);
  InterfacePair getActiveAlternateInterface(NetworkType network, unsigned short family);

  bool enable_drain_post_dns_refresh_{false};
  bool enable_interface_binding_{false};
  absl::flat_hash_set<std::string> hosts_to_drain_;
  Extensions::Common::DynamicForwardProxy::DnsCache::AddUpdateCallbacksHandlePtr
      dns_callbacks_handle_{nullptr};
  Upstream::ClusterManager& cluster_manager_;
  DnsCacheManagerSharedPtr dns_cache_manager_;
  ProxySettingsConstSharedPtr proxy_settings_;
  static NetworkState network_state_;
};

using ConnectivityManagerSharedPtr = std::shared_ptr<ConnectivityManager>;

/**
 * Provides access to the singleton ConnectivityManager.
 */
class ConnectivityManagerFactory {
public:
  ConnectivityManagerFactory(Server::Configuration::GenericFactoryContext& context)
      : context_(context) {}

  /**
   * @returns singleton ConnectivityManager instance.
   */
  ConnectivityManagerSharedPtr get();

private:
  Server::GenericFactoryContextImpl context_;
};

/**
 * Provides nullable access to the singleton ConnectivityManager.
 */
class ConnectivityManagerHandle {
public:
  ConnectivityManagerHandle(Singleton::Manager& singleton_manager)
      : singleton_manager_(singleton_manager) {}

  /**
   * @returns singleton ConnectivityManager instance. Can be nullptr if it hasn't been created.
   */
  ConnectivityManagerSharedPtr get();

private:
  Singleton::Manager& singleton_manager_;
};

} // namespace Network
} // namespace Envoy
