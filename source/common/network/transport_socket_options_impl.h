#pragma once

#include "envoy/network/proxy_protocol.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

class TransportSocketOptionsImpl : public TransportSocketOptions {
public:
  TransportSocketOptionsImpl(
      absl::string_view override_server_name = "",
      std::vector<std::string>&& override_verify_san_list = {},
      std::vector<std::string>&& override_alpn = {},
      absl::optional<Network::ProxyProtocolHeader> proxy_proto_header = absl::nullopt)
      : override_server_name_(override_server_name.empty()
                                  ? absl::nullopt
                                  : absl::optional<std::string>(override_server_name)),
        override_verify_san_list_{std::move(override_verify_san_list)},
        override_alpn_list_{std::move(override_alpn)}, proxy_protocol_header_(proxy_proto_header) {}

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
  absl::optional<Network::ProxyProtocolHeader> proxyProtocolHeader() const override {
    return proxy_protocol_header_;
  }
  void hashKey(std::vector<uint8_t>& key) const override;

private:
  const absl::optional<std::string> override_server_name_;
  const std::vector<std::string> override_verify_san_list_;
  const std::vector<std::string> override_alpn_list_;
  const absl::optional<Network::ProxyProtocolHeader> proxy_protocol_header_;
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
   * and ApplicationProtocols key in the filter state as well as optional PROXY protocol header
   *
   * @returns TransportSocketOptionsSharedPtr a shared pointer to the transport socket options,
   * nullptr if nothing is in the filter state or no PROXY protocol info is supplied.
   */
  static TransportSocketOptionsSharedPtr fromFilterStateWithProxyProtocolHeader(
      const StreamInfo::FilterState& stream_info,
      absl::optional<Network::ProxyProtocolHeader> proxy_proto_headers);
};

} // namespace Network
} // namespace Envoy
