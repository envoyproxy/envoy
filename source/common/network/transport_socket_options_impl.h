#pragma once

#include "envoy/common/optref.h"
#include "envoy/network/proxy_protocol.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

// A wrapper around another TransportSocketOptions that overrides the ALPN fallback.
class AlpnDecoratingTransportSocketOptions : public TransportSocketOptions {
public:
  AlpnDecoratingTransportSocketOptions(std::vector<std::string>&& alpn,
                                       TransportSocketOptionsConstSharedPtr inner_options)
      : alpn_fallback_(std::move(alpn)), inner_options_(std::move(inner_options)) {}
  // Network::TransportSocketOptions
  const absl::optional<std::string>& serverNameOverride() const override {
    return inner_options_->serverNameOverride();
  }
  const std::vector<std::string>& verifySubjectAltNameListOverride() const override {
    return inner_options_->verifySubjectAltNameListOverride();
  }
  const std::vector<std::string>& applicationProtocolListOverride() const override {
    return inner_options_->applicationProtocolListOverride();
  }
  const std::vector<std::string>& applicationProtocolFallback() const override {
    return alpn_fallback_;
  }
  absl::optional<Network::ProxyProtocolData> proxyProtocolOptions() const override {
    return inner_options_->proxyProtocolOptions();
  }
  OptRef<const Http11ProxyInfo> http11ProxyInfo() const override {
    return inner_options_->http11ProxyInfo();
  }
  const StreamInfo::FilterState::Objects& downstreamSharedFilterStateObjects() const override {
    return inner_options_->downstreamSharedFilterStateObjects();
  }

private:
  const std::vector<std::string> alpn_fallback_;
  const TransportSocketOptionsConstSharedPtr inner_options_;
};

class ServerNameDecoratingTransportSocketOptions : public TransportSocketOptions {
public:
  ServerNameDecoratingTransportSocketOptions(absl::string_view server_name_override,
                                             TransportSocketOptionsConstSharedPtr inner_options)
      : server_name_override_(server_name_override.empty()
                                  ? absl::nullopt
                                  : absl::optional<std::string>(server_name_override)),
        inner_options_(std::move(inner_options)) {}

  const absl::optional<std::string>& serverNameOverride() const override {
    return server_name_override_;
  }
  const std::vector<std::string>& verifySubjectAltNameListOverride() const override {
    return inner_options_->verifySubjectAltNameListOverride();
  }
  const std::vector<std::string>& applicationProtocolListOverride() const override {
    return inner_options_->applicationProtocolListOverride();
  }
  const std::vector<std::string>& applicationProtocolFallback() const override {
    return inner_options_->applicationProtocolFallback();
  }
  absl::optional<Network::ProxyProtocolData> proxyProtocolOptions() const override {
    return inner_options_->proxyProtocolOptions();
  }
  OptRef<const Http11ProxyInfo> http11ProxyInfo() const override {
    return inner_options_->http11ProxyInfo();
  }
  const StreamInfo::FilterState::Objects& downstreamSharedFilterStateObjects() const override {
    return inner_options_->downstreamSharedFilterStateObjects();
  }

private:
  const absl::optional<std::string> server_name_override_;
  const TransportSocketOptionsConstSharedPtr inner_options_;
};

class SubjectAltNamesDecoratingTransportSocketOptions : public TransportSocketOptions {
public:
  SubjectAltNamesDecoratingTransportSocketOptions(
      std::vector<std::string>&& verify_subject_alt_name_list_override,
      TransportSocketOptionsConstSharedPtr inner_options)
      : verify_subject_alt_name_list_override_(std::move(verify_subject_alt_name_list_override)),
        inner_options_(std::move(inner_options)) {}

  const absl::optional<std::string>& serverNameOverride() const override {
    return inner_options_->serverNameOverride();
  }
  const std::vector<std::string>& verifySubjectAltNameListOverride() const override {
    return verify_subject_alt_name_list_override_;
  }
  const std::vector<std::string>& applicationProtocolListOverride() const override {
    return inner_options_->applicationProtocolListOverride();
  }
  const std::vector<std::string>& applicationProtocolFallback() const override {
    return inner_options_->applicationProtocolFallback();
  }
  absl::optional<Network::ProxyProtocolData> proxyProtocolOptions() const override {
    return inner_options_->proxyProtocolOptions();
  }
  OptRef<const Http11ProxyInfo> http11ProxyInfo() const override {
    return inner_options_->http11ProxyInfo();
  }
  const StreamInfo::FilterState::Objects& downstreamSharedFilterStateObjects() const override {
    return inner_options_->downstreamSharedFilterStateObjects();
  }

private:
  const std::vector<std::string> verify_subject_alt_name_list_override_;
  const TransportSocketOptionsConstSharedPtr inner_options_;
};

class TransportSocketOptionsImpl : public TransportSocketOptions {
public:
  TransportSocketOptionsImpl(
      absl::string_view override_server_name = "",
      std::vector<std::string>&& override_verify_san_list = {},
      std::vector<std::string>&& override_alpn = {}, std::vector<std::string>&& fallback_alpn = {},
      absl::optional<Network::ProxyProtocolData> proxy_proto_options = absl::nullopt,
      StreamInfo::FilterState::ObjectsPtr filter_state_objects =
          std::make_unique<StreamInfo::FilterState::Objects>(),
      std::unique_ptr<const Http11ProxyInfo>&& proxy_info = nullptr)
      : override_server_name_(override_server_name.empty()
                                  ? absl::nullopt
                                  : absl::optional<std::string>(override_server_name)),
        override_verify_san_list_{std::move(override_verify_san_list)},
        override_alpn_list_{std::move(override_alpn)}, alpn_fallback_{std::move(fallback_alpn)},
        proxy_protocol_options_(proxy_proto_options),
        filter_state_objects_(std::move(filter_state_objects)), proxy_info_(std::move(proxy_info)) {
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
  const std::vector<std::string>& applicationProtocolFallback() const override {
    return alpn_fallback_;
  }
  absl::optional<Network::ProxyProtocolData> proxyProtocolOptions() const override {
    return proxy_protocol_options_;
  }
  OptRef<const Http11ProxyInfo> http11ProxyInfo() const override {
    if (!proxy_info_) {
      return {};
    }
    return {*proxy_info_};
  }
  const StreamInfo::FilterState::Objects& downstreamSharedFilterStateObjects() const override {
    return *filter_state_objects_;
  }

private:
  const absl::optional<std::string> override_server_name_;
  const std::vector<std::string> override_verify_san_list_;
  const std::vector<std::string> override_alpn_list_;
  const std::vector<std::string> alpn_fallback_;
  const absl::optional<Network::ProxyProtocolData> proxy_protocol_options_;
  const StreamInfo::FilterState::ObjectsPtr filter_state_objects_;
  const StreamInfo::FilterStateSharedPtr filter_state_;
  std::unique_ptr<const Http11ProxyInfo> proxy_info_;
};

class TransportSocketOptionsUtility {
public:
  /**
   * Construct TransportSocketOptions from StreamInfo::FilterState, using UpstreamServerName
   * and ApplicationProtocols key in the filter state.
   * @returns TransportSocketOptionsConstSharedPtr a shared pointer to the transport socket options,
   * nullptr if nothing is in the filter state.
   */
  static TransportSocketOptionsConstSharedPtr
  fromFilterState(const StreamInfo::FilterState& stream_info);
};

class CommonUpstreamTransportSocketFactory : public UpstreamTransportSocketFactory {
public:
  /**
   * Compute the generic hash key from the transport socket options.
   */
  void hashKey(std::vector<uint8_t>& key,
               TransportSocketOptionsConstSharedPtr options) const override;
};

} // namespace Network
} // namespace Envoy
