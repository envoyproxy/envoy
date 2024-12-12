#include "test/common/proxy/test_apple_proxy_resolver.h"

namespace Envoy {
namespace Network {

void TestAppleProxyResolver::setSettingsMonitorForTest(
    std::unique_ptr<Network::AppleSystemProxySettingsMonitor>&& monitor) {
  proxy_settings_monitor_ = std::move(monitor);
}

} // namespace Network
} // namespace Envoy
