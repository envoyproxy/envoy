#pragma once

#include "library/cc/network_change_monitor.h"

#ifdef __OBJC__
@class EnvoyCxxNetworkMonitor;
#else
typedef void EnvoyCxxNetworkMonitor;
#endif

namespace Envoy {
namespace Platform {

/**
 * Apple-specific implementation of NetworkChangeMonitor.
 * Monitors network connectivity changes using Apple's network framework.
 */
class AppleNetworkChangeMonitor : public NetworkChangeMonitor {
public:
  /**
   * Construct an AppleNetworkChangeMonitor.
   * @param engine reference to a NetworkChangeListener.
   */
  explicit AppleNetworkChangeMonitor(NetworkChangeListener& engine);

  ~AppleNetworkChangeMonitor() override;

  // NetworkChangeMonitor
  void start() override;
  void stop() override;

private:
  NetworkChangeListener& network_change_listener_;
  EnvoyCxxNetworkMonitor* monitor_impl_; // Objective-C implementation
};

} // namespace Platform
} // namespace Envoy
