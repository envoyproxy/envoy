#pragma once

#include "library/common/network/apple_pac_proxy_resolver.h"
#include "library/common/network/apple_proxy_resolver.h"
#include "library/common/network/apple_system_proxy_settings_monitor.h"

namespace Envoy {
namespace Network {

class TestAppleProxyResolver : public Network::AppleProxyResolver {
public:
  /**
   * Resets the proxy settings monitor to the supplied monitor instance.
   * For tests only.
   */
  void
  setSettingsMonitorForTest(std::unique_ptr<Network::AppleSystemProxySettingsMonitor>&& monitor);

  /**
   * Resets the PAC URL resolver to the supplied instance.
   * For tests only.
   */
  void setPacResolverForTest(std::unique_ptr<Network::ApplePacProxyResolver>&& resolver);
};

} // namespace Network
} // namespace Envoy
