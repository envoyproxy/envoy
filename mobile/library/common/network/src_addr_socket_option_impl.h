#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/listen_socket.h"

namespace Envoy {
namespace Network {

/**
 * This is a "synthetic" socket option implementation, which sets the source IP/port of a socket
 * using a provided IP address (and maybe port) during bind.
 *
 * Based on the OriginalSrcSocketOption extension.
 */
class SrcAddrSocketOptionImpl : public Network::Socket::Option {
public:
  /**
   * Constructs a socket option which will set the socket to use source @c source_address
   */
  SrcAddrSocketOptionImpl(Network::Address::InstanceConstSharedPtr source_address);
  ~SrcAddrSocketOptionImpl() override = default;

  /**
   * Updates the source address of the socket to match `source_address_`.
   * Adds socket options to the socket to allow this to work.
   */
  bool setOption(Network::Socket& socket,
                 envoy::config::core::v3::SocketOption::SocketState state) const override;

  /**
   * Appends a key which uniquely identifies the address being tracked.
   */
  void hashKey(std::vector<uint8_t>& key) const override;

  absl::optional<Details>
  getOptionDetails(const Network::Socket& socket,
                   envoy::config::core::v3::SocketOption::SocketState state) const override;
  bool isSupported() const override { return true; }

private:
  Network::Address::InstanceConstSharedPtr source_address_;
};

} // namespace Network
} // namespace Envoy
