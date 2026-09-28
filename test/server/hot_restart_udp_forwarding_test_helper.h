#pragma once

#include "source/server/hot_restart_impl.h"
#include "source/server/hot_restarting_parent.h"

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
  std::optional<HotRestartingChild::UdpForwardingContext::ForwardEntry>
  getListenerForDestination(const Network::Address::Instance& address) {
    return child_.udp_forwarding_context_.getListenerForDestination(address);
  }
  int mainSocketFd() const { return child_.main_rpc_stream_.domain_socket_; }
  int udpForwardingSocketFd() const { return child_.udp_forwarding_rpc_stream_.domain_socket_; }
  void setParentReplyTimeout(std::chrono::milliseconds timeout) {
    child_.parent_reply_timeout_ = timeout;
  }
  void setParentProbeInterval(std::chrono::milliseconds interval) {
    child_.parent_probe_interval_ = interval;
  }
  bool parentUnresponsive() const { return child_.parent_unresponsive_; }
  bool parentTerminated() const { return child_.parent_terminated_; }

private:
  HotRestartingChild& child_;
};

class HotRestartParentUdpForwardingTestHelper {
public:
  explicit HotRestartParentUdpForwardingTestHelper(HotRestartingParent& parent) : parent_(parent) {}
  int udpForwardingSocketFd() const { return parent_.udp_forwarding_rpc_stream_.domain_socket_; }
  uint64_t queuedDatagrams() const {
    return parent_.udp_forwarding_rpc_stream_.sendQueueDatagrams();
  }
  uint64_t queuedBytes() const { return parent_.udp_forwarding_rpc_stream_.sendQueueBytes(); }

private:
  HotRestartingParent& parent_;
};

} // namespace Server
} // namespace Envoy
