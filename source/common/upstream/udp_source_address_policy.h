#pragma once

#include "envoy/network/address.h"
#include "envoy/network/socket.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

/**
 * Describes how a UDP upstream socket obtains its source address.
 *
 * Kernel-selected and configured policies can use a connected UDP socket. A transparent policy
 * leaves the socket unconnected and supplies the downstream IP on each send.
 */
class UdpSourceAddressPolicy {
public:
  enum class Mode { KernelSelected, Configured, Transparent };

  struct SocketSetupResult {
    enum class FailureReason { None, SocketOption, Bind };

    bool ok() const { return failure_reason_ == FailureReason::None; }

    FailureReason failure_reason_{FailureReason::None};
    int sys_errno_{};
  };

  static UdpSourceAddressPolicy kernelSelected();
  static UdpSourceAddressPolicy
  fromUpstreamLocalAddress(UpstreamLocalAddress upstream_local_address);
  static UdpSourceAddressPolicy
  transparent(Network::Address::InstanceConstSharedPtr source_address);

  Mode mode() const { return mode_; }
  bool shouldConnect() const { return mode_ != Mode::Transparent; }
  const Network::Address::Ip* packetSourceIp() const;
  const Network::Address::InstanceConstSharedPtr& sourceAddress() const {
    return upstream_local_address_.address_;
  }

  SocketSetupResult prepareSocket(Network::Socket& socket) const;

private:
  UdpSourceAddressPolicy(Mode mode, UpstreamLocalAddress upstream_local_address,
                         Network::Address::InstanceConstSharedPtr packet_source_address);

  Mode mode_;
  UpstreamLocalAddress upstream_local_address_;
  Network::Address::InstanceConstSharedPtr packet_source_address_;
};

} // namespace Upstream
} // namespace Envoy
