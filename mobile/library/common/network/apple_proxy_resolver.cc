#include "library/common/network/apple_proxy_resolver.h"

namespace Envoy {
namespace Network {

AppleProxyResolver::AppleProxyResolver()
    : proxy_settings_monitor_(
          std::make_unique<AppleSystemProxySettingsMonitor>(proxySettingsUpdater())),
      pac_proxy_resolver_(std::make_unique<ApplePACProxyResolver>()) {}

std::function<void(absl::optional<Network::SystemProxySettings>)>
AppleProxyResolver::proxySettingsUpdater() {
  return std::function<void(absl::optional<Network::SystemProxySettings>)>(
      [this](absl::optional<SystemProxySettings> proxy_settings) {
        absl::MutexLock l(&mutex_);
        proxy_settings_ = proxy_settings;
      });
}

void AppleProxyResolver::setSettingsMonitorForTest(
    std::unique_ptr<AppleSystemProxySettingsMonitor>&& monitor) {
  proxy_settings_monitor_ = std::move(monitor);
}

void AppleProxyResolver::start() { proxy_settings_monitor_->start(); }

envoy_proxy_resolution_result
AppleProxyResolver::resolveProxy(std::string target_url_string, std::vector<ProxySettings>& proxies,
                                 std::function<void(std::vector<ProxySettings>)> completion) {
  absl::MutexLock l(&mutex_);
  if (!proxy_settings_.has_value()) {
    return ENVOY_PROXY_RESOLUTION_RESULT_NO_PROXY_CONFIGURED;
  }

  const auto proxy_settings = proxy_settings_.value();
  if (!proxy_settings.isPACEnabled()) {
    auto settings = ProxySettings(proxy_settings.hostname(), proxy_settings.port());
    if (settings.isDirect()) {
      return ENVOY_PROXY_RESOLUTION_RESULT_NO_PROXY_CONFIGURED;
    } else {
      proxies.push_back(settings);
      return ENVOY_PROXY_RESOLUTION_RESULT_COMPLETED;
    }
  }

  pac_proxy_resolver_->resolveProxies(
      proxy_settings.pacFileURL(), target_url_string,
      [completion](std::vector<ProxySettings> proxies) { completion(proxies); });
  return ENVOY_PROXY_RESOLUTION_RESULT_IN_PROGRESS;
}

} // namespace Network
} // namespace Envoy
