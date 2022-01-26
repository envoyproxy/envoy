#pragma once

#include "envoy/network/proxy_protocol.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

// Base wrapper for transport socket options.
class BaseWrapperTransportSocketOptions : public TransportSocketOptions {
public:
  BaseWrapperTransportSocketOptions(TransportSocketOptionsConstSharedPtr inner_options)
      : inner_options_(std::move(inner_options)) {}
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
    return inner_options_->applicationProtocolFallback();
  }
  absl::optional<Network::ProxyProtocolData> proxyProtocolOptions() const override {
    return inner_options_->proxyProtocolOptions();
  }
  void hashKey(std::vector<uint8_t>& key,
               const Network::TransportSocketFactory& factory) const override {
    return inner_options_->hashKey(key, factory);
  }
  std::shared_ptr<const Upstream::HostDescription> host() const override {
    return inner_options_->host();
  }

private:
  const TransportSocketOptionsConstSharedPtr inner_options_;
};

// A wrapper around another TransportSocketOptions that overrides the ALPN fallback.
class AlpnDecoratingTransportSocketOptions : public BaseWrapperTransportSocketOptions {
public:
  AlpnDecoratingTransportSocketOptions(std::vector<std::string>&& alpn,
                                       TransportSocketOptionsConstSharedPtr inner_options)
      : BaseWrapperTransportSocketOptions(std::move(inner_options)),
        alpn_fallback_(std::move(alpn)) {}
  const std::vector<std::string>& applicationProtocolFallback() const override {
    return alpn_fallback_;
  }
  void hashKey(std::vector<uint8_t>& key,
               const Network::TransportSocketFactory& factory) const override;

private:
  const std::vector<std::string> alpn_fallback_;
};

// A wrapper around another TransportSocketOptions that overrides the upstream host.
class HostDecoratingTransportSocketOptions : public BaseWrapperTransportSocketOptions {
public:
  HostDecoratingTransportSocketOptions(std::shared_ptr<const Upstream::HostDescription>& host,
                                       TransportSocketOptionsConstSharedPtr inner_options)
      : BaseWrapperTransportSocketOptions(std::move(inner_options)), host_(host) {}
  std::shared_ptr<const Upstream::HostDescription> host() const override { return host_; }

private:
  const std::shared_ptr<const Upstream::HostDescription> host_;
};

class TransportSocketOptionsImpl : public TransportSocketOptions {
public:
  TransportSocketOptionsImpl(
      absl::string_view override_server_name = "",
      std::vector<std::string>&& override_verify_san_list = {},
      std::vector<std::string>&& override_alpn = {}, std::vector<std::string>&& fallback_alpn = {},
      absl::optional<Network::ProxyProtocolData> proxy_proto_options = absl::nullopt)
      : override_server_name_(override_server_name.empty()
                                  ? absl::nullopt
                                  : absl::optional<std::string>(override_server_name)),
        override_verify_san_list_{std::move(override_verify_san_list)},
        override_alpn_list_{std::move(override_alpn)}, alpn_fallback_{std::move(fallback_alpn)},
        proxy_protocol_options_(proxy_proto_options) {}

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
  void hashKey(std::vector<uint8_t>& key,
               const Network::TransportSocketFactory& factory) const override;

  std::shared_ptr<const Upstream::HostDescription> host() const override { return nullptr; }

private:
  const absl::optional<std::string> override_server_name_;
  const std::vector<std::string> override_verify_san_list_;
  const std::vector<std::string> override_alpn_list_;
  const std::vector<std::string> alpn_fallback_;
  const absl::optional<Network::ProxyProtocolData> proxy_protocol_options_;
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

} // namespace Network
} // namespace Envoy
