#pragma once

#include "source/server/hot_restart_impl.h"

namespace Envoy {
namespace Server {

class HotRestartUdpForwardingTestHelper {
public:
  explicit HotRestartUdpForwardingTestHelper(HotRestartImpl& hot_restart)
      : child_(hot_restart.as_child_) {}
  explicit HotRestartUdpForwardingTestHelper(HotRestartingChild& child) : child_(child) {}
  void registerUdpForwardingListener(Network::Address::InstanceConstSharedPtr address,
                                     std::shared_ptr<Network::UdpListenerConfig> listener_config) {
    child_.registerUdpForwardingListener(address, listener_config);
  }
  absl::optional<HotRestartingChild::UdpForwardingContext::ForwardEntry>
  getListenerForDestination(const Network::Address::Instance& address) {
    return child_.udp_forwarding_context_.getListenerForDestination(address);
  }

private:
  HotRestartingChild& child_;
};

} // namespace Server
} // namespace Envoy
