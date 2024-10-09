#pragma once

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>

#include <functional>

#include "absl/types/optional.h"
#include "library/common/network/proxy_settings.h"

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
   * @param proxy_settings_read_callback The closure to call every time system proxy settings
   *                                     change. The closure is called on a non-main queue.
   */
  AppleSystemProxySettingsMonitor(SystemProxySettingsReadCallback proxy_settings_read_callback);
  virtual ~AppleSystemProxySettingsMonitor();

  /**
   * Starts monitoring system proxy settings.
   */
  void start();

protected:
  // Protected and virtual so they can be overridden by tests to provide mocked system proxy
  // settings.
  virtual CFDictionaryRef getSystemProxySettings() const;

private:
  absl::optional<SystemProxySettings> readSystemProxySettings() const;

  dispatch_source_t source_;
  dispatch_queue_t queue_;
  bool started_ = false;
  SystemProxySettingsReadCallback proxy_settings_read_callback_;
};

} // namespace Network
} // namespace Envoy
