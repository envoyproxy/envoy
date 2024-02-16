#include "test/common/proxy/test_apple_proxy_resolver.h"

namespace Envoy {
namespace Network {

void TestAppleProxyResolver::setSettingsMonitorForTest(
    std::unique_ptr<Network::AppleSystemProxySettingsMonitor>&& monitor) {
  proxy_settings_monitor_ = std::move(monitor);
}

void TestAppleProxyResolver::setPacResolverForTest(
    std::unique_ptr<Network::ApplePacProxyResolver>&& resolver) {
  pac_proxy_resolver_ = std::move(resolver);
}

} // namespace Network
} // namespace Envoy
