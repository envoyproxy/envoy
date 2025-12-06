#pragma once

#include "library/cc/engine.h"
#include "library/cc/network_change_monitor.h"

#ifdef __OBJC__
@class EnvoyNetworkMonitor;
#else
typedef void EnvoyNetworkMonitor;
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
   * @param engine pointer to the Envoy engine.
   */
  explicit AppleNetworkChangeMonitor(Engine* engine);
  
  ~AppleNetworkChangeMonitor() override;

  // NetworkChangeMonitor
  void start() override;
  void stop() override;

private:
  Engine* engine_;
  EnvoyNetworkMonitor* monitor_impl_; // Objective-C implementation
};

} // namespace Platform
} // namespace Envoy
