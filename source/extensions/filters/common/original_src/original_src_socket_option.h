#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/listen_socket.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace OriginalSrc {
/**
 * A socket option implementation which allows a connection to spoof its source IP/port using
 * a provided IP address (and maybe port).
 */
class OriginalSrcSocketOption : public Network::Socket::Option {
public:
  /**
   * Constructs a socket option which will set the socket to use source @c src_address
   */
  OriginalSrcSocketOption(Network::Address::InstanceConstSharedPtr src_address);
  ~OriginalSrcSocketOption() override = default;

  /**
   * Updates the source address of the socket to match `src_address_`.
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
  Network::Address::InstanceConstSharedPtr src_address_;
};

} // namespace OriginalSrc
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
