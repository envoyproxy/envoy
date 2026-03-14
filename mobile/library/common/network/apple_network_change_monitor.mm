#include <dispatch/dispatch.h>

#include <memory>

#include "library/common/network/apple_network_change_monitor.h"
#include "library/common/network/apple_network_change_monitor_impl.h"

namespace Envoy {
namespace Platform {

namespace {

class ForwardingNetworkChangeListener : public NetworkChangeListener {
public:
  explicit ForwardingNetworkChangeListener(NetworkChangeListener &network_change_listener)
      : network_change_listener_(network_change_listener) {}

  void onDefaultNetworkChangeEvent(int network) override {
    network_change_listener_.onDefaultNetworkChangeEvent(network);
  }

  void onDefaultNetworkAvailable() override {
    network_change_listener_.onDefaultNetworkAvailable();
  }

  void onDefaultNetworkUnavailable() override {
    network_change_listener_.onDefaultNetworkUnavailable();
  }

private:
  NetworkChangeListener &network_change_listener_;
};

} // namespace

AppleNetworkChangeMonitor::AppleNetworkChangeMonitor(NetworkChangeListener &network_change_listener)
    : network_change_listener_impl_(
          std::make_shared<ForwardingNetworkChangeListener>(network_change_listener)),
      monitor_impl_(nil) {}

AppleNetworkChangeMonitor::~AppleNetworkChangeMonitor() { stop(); }

void AppleNetworkChangeMonitor::start() {
  if (monitor_impl_ != nil) {
    return; // Already started
  }

  // Create the Objective-C network monitor on the main dispatch queue
  dispatch_queue_t queue = dispatch_get_main_queue();
  monitor_impl_ = [[EnvoyCxxNetworkMonitor alloc] initWithListener:network_change_listener_impl_
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
