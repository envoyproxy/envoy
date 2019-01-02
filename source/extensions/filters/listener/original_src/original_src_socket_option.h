#pragma once

#include "envoy/network/address.h"
#include "envoy/network/listen_socket.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {
/**
 * A socket option implementation which allows a connection to spoof its source IP/port using
 * a provided IP address (and maybe port).
 */
class OriginalSrcSocketOption : public Network::Socket::Option {
public:
  OriginalSrcSocketOption(Network::Address::InstanceConstSharedPtr src_address);
  ~OriginalSrcSocketOption() {}

  /**
   * Updates the source address of the socket to match @c src_address_.
   * Adds socket options to the socket to allow this to work.
   */
  bool setOption(Network::Socket& socket,
                 envoy::api::v2::core::SocketOption::SocketState state) const override;

  /**
   * Appends a key which uniquely identifies the address being tracked.
   */
  void hashKey(std::vector<uint8_t>& key) const override;

  absl::optional<Details>
  getOptionDetails(const Network::Socket& socket,
                   envoy::api::v2::core::SocketOption::SocketState state) const override;

  static constexpr uint8_t IPV4_KEY = 0;
  static constexpr uint8_t IPV6_KEY = 1;

private:
  Network::Address::InstanceConstSharedPtr src_address_;
  Network::Socket::Options options_to_apply_;
};

} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
