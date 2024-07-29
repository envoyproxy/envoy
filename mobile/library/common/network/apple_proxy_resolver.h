#pragma once

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>

#include <functional>
#include <memory>

#include "library/common/network/apple_pac_proxy_resolver.h"
#include "library/common/network/apple_system_proxy_settings_monitor.h"
#include "library/common/network/proxy_resolver_interface.h"
#include "library/common/network/proxy_settings.h"

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

  /**
   * Resolves the proxy settings for the target URL. The result of proxy resolution is returned in
   * the ProxyResolutionResult enum. If proxy resolution returns RESULT_COMPLETED, the `proxies`
   * vector gets populated with the resolved proxy setting. If proxy resolution returns
   * RESULT_IN_PROGRESS, the `proxy_resolution_completed` function gets invoked upon successful
   * resolution of the proxy settings.
   */
  virtual ProxyResolutionResult
  resolveProxy(const std::string& target_url_string, std::vector<ProxySettings>& proxies,
               ProxySettingsResolvedCallback proxy_resolution_completed) override;

  /*
   * Supplies a function that updates this instance's proxy settings.
   */
  SystemProxySettingsReadCallback proxySettingsUpdater();

private:
  friend class TestAppleProxyResolver;

  std::unique_ptr<AppleSystemProxySettingsMonitor> proxy_settings_monitor_;
  std::unique_ptr<ApplePacProxyResolver> pac_proxy_resolver_;
  absl::optional<SystemProxySettings> proxy_settings_;
  absl::Mutex mutex_;
  bool started_ = false;
};

} // namespace Network
} // namespace Envoy
