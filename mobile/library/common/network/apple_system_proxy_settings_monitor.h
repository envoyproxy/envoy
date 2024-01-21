#pragma once

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>

#include <functional>

#include "absl/types/optional.h"
#include "library/common/network/system_proxy_settings.h"

namespace Envoy {
namespace Network {

/**
 * Monitors Apple system proxy settings changes. Optimized for iOS platform.
 */
class AppleSystemProxySettingsMonitor {
public:
  /**
   * Creates a new instance of the receiver. Calls provided function every time it detects a
   * change of system proxy settings. Treats invalid PAC URLs as a lack of proxy configuration.
   *
   * @param proxySettingsDidUpdate The closure to call every time system proxy settings change. The
   * closure is called on a non-main queue.
   */
  AppleSystemProxySettingsMonitor(
      std::function<void(absl::optional<SystemProxySettings>)> proxySettingsDidUpdate)
      : proxySettingsDidUpdate_(proxySettingsDidUpdate){};
  virtual ~AppleSystemProxySettingsMonitor();

  /**
   * Starts monitoring system proxy settings.
   */
  void start();

protected:
  // Protected and virtual so they can be overridden by tests to provide mocked system proxy settings.
  virtual CFDictionaryRef getSystemProxySettings() const;

private:
  absl::optional<SystemProxySettings> readSystemProxySettings() const;

  dispatch_source_t source_;
  dispatch_queue_t queue_;
  bool started_;
  std::function<void(absl::optional<SystemProxySettings>)> proxySettingsDidUpdate_;
};

} // namespace Network
} // namespace Envoy
