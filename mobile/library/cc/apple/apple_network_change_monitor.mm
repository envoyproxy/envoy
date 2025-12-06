#include "library/cc/apple/apple_network_change_monitor.h"
#include "library/cc/apple/apple_network_change_monitor_bridge.h"

#include <dispatch/dispatch.h>

namespace Envoy {
namespace Platform {

AppleNetworkChangeMonitor::AppleNetworkChangeMonitor(Engine* engine)
    : engine_(engine), monitor_impl_(nil) {}

AppleNetworkChangeMonitor::~AppleNetworkChangeMonitor() {
  stop();
}

void AppleNetworkChangeMonitor::start() {
  if (monitor_impl_ != nil) {
    return; // Already started
  }
  
  // Get the shared engine instance from the C++ wrapper
  // We need to create a shared_ptr from the raw pointer for the Objective-C implementation
  std::shared_ptr<Envoy::Platform::Engine> engine_ptr(engine_, [](Engine*) {
    // Empty deleter - we don't own the engine
  });
  
  // Create the Objective-C network monitor on the main dispatch queue
  dispatch_queue_t queue = dispatch_get_main_queue();
  monitor_impl_ = [[HAMEnvoyNetworkMonitor alloc] initWithEngine:engine_ptr
                                               defaultDelegateQueue:queue
                                          ignoreUpdateOnSameNetwork:NO
                                           useNewNetworkChangeEvent:NO];
}

void AppleNetworkChangeMonitor::stop() {
  if (monitor_impl_ != nil) {
    [monitor_impl_ stop];
    monitor_impl_ = nil;
  }
}

} // namespace Platform
} // namespace Envoy