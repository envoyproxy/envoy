#pragma once

#include "envoy/network/transport_socket.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

class TransportSocketOptionsImpl : public TransportSocketOptions {
public:
  TransportSocketOptionsImpl(absl::string_view override_server_name = "",
                             std::vector<std::string>&& override_verify_san_list = {},
                             std::vector<std::string>&& override_alpn = {},
                             absl::optional<Network::TransportSocketOptions::DownstreamAddresses>
                                 downstream_addresses = absl::nullopt)
      : override_server_name_(override_server_name.empty()
                                  ? absl::nullopt
                                  : absl::optional<std::string>(override_server_name)),
        override_verify_san_list_{std::move(override_verify_san_list)},
        override_alpn_list_{std::move(override_alpn)}, downstream_addresses_(downstream_addresses) {
  }

  // Network::TransportSocketOptions
  const absl::optional<std::string>& serverNameOverride() const override {
    return override_server_name_;
  }
  const std::vector<std::string>& verifySubjectAltNameListOverride() const override {
    return override_verify_san_list_;
  }
  const std::vector<std::string>& applicationProtocolListOverride() const override {
    return override_alpn_list_;
  }
  const absl::optional<Network::TransportSocketOptions::DownstreamAddresses>
  downstreamAddresses() const override {
    return downstream_addresses_;
  }
  void hashKey(std::vector<uint8_t>& key) const override;

private:
  const absl::optional<std::string> override_server_name_;
  const std::vector<std::string> override_verify_san_list_;
  const std::vector<std::string> override_alpn_list_;
  const absl::optional<Network::TransportSocketOptions::DownstreamAddresses> downstream_addresses_;
};

class TransportSocketOptionsUtility {
public:
  /**
   * Construct TransportSocketOptions from StreamInfo::FilterState, using UpstreamServerName
   * and ApplicationProtocols key in the filter state.
   * @returns TransportSocketOptionsSharedPtr a shared pointer to the transport socket options,
   * nullptr if nothing is in the filter state.
   */
  static TransportSocketOptionsSharedPtr
  fromFilterState(const StreamInfo::FilterState& stream_info);
  /**
   * Construct TransportSocketOptions from StreamInfo::FilterState, using UpstreamServerName
   * and ApplicationProtocols key in the filter state as well as optional downstream address
   * info.
   * @returns TransportSocketOptionsSharedPtr a shared pointer to the transport socket options,
   * nullptr if nothing is in the filter state or no downstream address info is supplied.
   */
  static TransportSocketOptionsSharedPtr fromFilterStateWithDownstreamAddrs(
      const StreamInfo::FilterState& stream_info,
      absl::optional<Network::TransportSocketOptions::DownstreamAddresses> down_addrs);
};

} // namespace Network
} // namespace Envoy
