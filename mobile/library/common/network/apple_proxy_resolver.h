#pragma once

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>

#include <functional>
#include <memory>

#include "library/common/network/apple_pac_proxy_resolver.h"
#include "library/common/network/apple_system_proxy_settings_monitor.h"
#include "library/common/network/system_proxy_settings.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Network {

/**
 * Resolves proxies on Apple platforms.
 */
class AppleProxyResolver {
public:
  AppleProxyResolver();

  /**
   * Starts proxy resolver. It needs to be called prior to any proxy resolution attempt.
   */
  void start();

  /**
   * Resolves proxy for a given url. Depending on the type current system proxy settings the method
   * may return results in synchronous or asynchronous way.
   * @param proxies Result proxies for when the proxy resolution is performed synchronously.
   * @param proxy_resolution_did_complete A function that's called with result proxies as its
   * arguments for when the proxy resolution is performed asynchronously.
   * @return Whether there is a proxy or no and whether proxy resolution was performed synchronously
   * or whether it's still running.
   */
  envoy_proxy_resolution_result resolveProxy(
      std::string target_url_string, std::vector<ProxySettings>& proxies,
      std::function<void(std::vector<ProxySettings> proxies)> proxy_resolution_did_complete);

  /*
   * Supplies a function that updates this instance's proxy settings.
   */
  std::function<void(absl::optional<Network::SystemProxySettings>)> proxySettingsUpdater();

  /**
   * Resets the proxy settings monitor to the supplied monitor instance.
   * For tests only.
   */
  void setSettingsMonitorForTest(std::unique_ptr<AppleSystemProxySettingsMonitor>&& monitor);

private:
  std::unique_ptr<AppleSystemProxySettingsMonitor> proxy_settings_monitor_;
  std::unique_ptr<ApplePACProxyResolver> pac_proxy_resolver_;
  absl::optional<SystemProxySettings> proxy_settings_;
  absl::Mutex mutex_;
};

} // namespace Network
} // namespace Envoy
