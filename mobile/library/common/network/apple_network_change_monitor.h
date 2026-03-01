#pragma once

#include <memory>

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
   * @param network_change_listener reference to a NetworkChangeListener.
   */
  explicit AppleNetworkChangeMonitor(NetworkChangeListener& network_change_listener);

  ~AppleNetworkChangeMonitor() override;

  // NetworkChangeMonitor
  void start() override;
  void stop() override;

private:
  // Owned forwarding listener retained for the lifetime of the Objective-C monitor callbacks.
  std::shared_ptr<NetworkChangeListener> network_change_listener_impl_;
  EnvoyCxxNetworkMonitor* monitor_impl_; // Objective-C implementation
};

} // namespace Platform
} // namespace Envoy
