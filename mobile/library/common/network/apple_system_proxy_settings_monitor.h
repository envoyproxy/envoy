#pragma once

#include <functional>
#include <dispatch/dispatch.h>
#include "library/common/network/system_proxy_settings.h"

namespace Envoy {
namespace Network {

class AppleSystemProxySettingsMonitor {
  AppleSystemProxySettingsMonitor(std::function<void(SystemProxySettings)> proxySettingsDidUpdate): proxySettingsDidUpdate_(proxySettingsDidUpdate) {};
  void start();
private:
  dispatch_queue_t queue_;
  bool started_;
  std::function<void(SystemProxySettings)> proxySettingsDidUpdate_;
};

} // namespace Network
} // namespace Envoy

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

void testProxy();

#ifdef __cplusplus
}
#endif
