#pragma once

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>

#include <functional>

#include "library/common/network/apple_pac_proxy_resolver.h"
#include "library/common/network/apple_system_proxy_settings_monitor.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Network {

class AppleProxyResolver {
  AppleProxyResolver();
  /**
   * Starts proxy resolver. It needs to be called prior to any proxy resolution attempt.
   */
  void start();

  envoy_proxy_resolution_result
  resolveProxy(std::string target_url_string, std::vector<ProxySettings>& proxies,
               std::function<void(std::vector<ProxySettings> proxies)>);

private:
  AppleSystemProxySettingsMonitor proxy_settings_monitor_;
  ApplePACProxyResolver pac_proxy_resolver_;
  absl::optional<SystemProxySettings> proxy_settings_;
  absl::Mutex mutex_;
};

} // namespace Network
} // namespace Envoy
