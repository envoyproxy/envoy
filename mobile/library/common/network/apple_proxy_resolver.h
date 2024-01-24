#pragma once

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>

#include <functional>
#include <memory>

#include "library/common/network/apple_pac_proxy_resolver.h"
#include "library/common/network/apple_system_proxy_settings_monitor.h"
#include "library/common/network/proxy_resolver_interface.h"
#include "library/common/network/proxy_types.h"
#include "library/common/network/system_proxy_settings.h"

namespace Envoy {
namespace Network {

/**
 * Resolves proxies on Apple platforms.
 */
class AppleProxyResolver : public ProxyResolver {
public:
  AppleProxyResolver();
  virtual ~AppleProxyResolver() = default;

  /**
   * Starts proxy resolver. It needs to be called prior to any proxy resolution attempt.
   */
  void start();

  virtual ProxyResolutionResult
  resolveProxy(const std::string& target_url_string, std::vector<ProxySettings>& proxies,
               std::function<void(std::vector<ProxySettings>& proxies)>
                   proxy_resolution_did_complete) override;

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
  std::unique_ptr<ApplePacProxyResolver> pac_proxy_resolver_;
  absl::optional<SystemProxySettings> proxy_settings_;
  absl::Mutex mutex_;
};

} // namespace Network
} // namespace Envoy
