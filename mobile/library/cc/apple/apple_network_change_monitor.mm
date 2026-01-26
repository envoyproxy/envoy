#include <dispatch/dispatch.h>

#include <memory>

#include "library/cc/apple/apple_network_change_monitor.h"
#include "library/cc/apple/apple_network_change_monitor_bridge.h"

namespace Envoy {
namespace Platform {

AppleNetworkChangeMonitor::AppleNetworkChangeMonitor(NetworkChangeListener &network_change_listener)
    : network_change_listener_(network_change_listener), monitor_impl_(nil) {}

AppleNetworkChangeMonitor::~AppleNetworkChangeMonitor() { stop(); }

void AppleNetworkChangeMonitor::start() {
  if (monitor_impl_ != nil) {
    return; // Already started
  }

  // Get the shared network change listener instance from the C++ wrapper
  // We need to create a shared_ptr from the raw pointer for the Objective-C implementation
  std::shared_ptr<Envoy::Platform::NetworkChangeListener> network_change_listener_ptr(
      &network_change_listener_, [](NetworkChangeListener *) {
        // Empty deleter - we don't own the network change listener
      });

  // Create the Objective-C network monitor on the main dispatch queue
  dispatch_queue_t queue = dispatch_get_main_queue();
  monitor_impl_ = [[EnvoyCxxNetworkMonitor alloc] initWithListener:network_change_listener_ptr
                                              defaultDelegateQueue:queue
                                         ignoreUpdateOnSameNetwork:NO];
}

void AppleNetworkChangeMonitor::stop() {
  if (monitor_impl_ != nil) {
    [monitor_impl_ stop];
    monitor_impl_ = nil;
  }
}

} // namespace Platform
} // namespace Envoy
