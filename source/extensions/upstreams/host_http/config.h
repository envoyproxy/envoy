#pragma once

#include "envoy/extensions/upstreams/host_http/v3/host_http.pb.h"
#include "envoy/extensions/upstreams/host_http/v3/host_http.pb.validate.h"
#include "envoy/server/filter_config.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace HostHttp {

/**
 * Compiled host-specific options with pre-constructed matchers.
 */
class HttpProtocolOptionsConfigImpl : public Upstream::HttpProtocolOptionsConfig {
public:
  HttpProtocolOptionsConfigImpl(
      const envoy::extensions::upstreams::host_http::v3::HostHttpProtocolOptions::HostOptions&
          options,
      Server::Configuration::CommonFactoryContext& context);

  ~HttpProtocolOptionsConfigImpl() = default;

  const Http::Http1Settings& http1Settings() const override;
  const envoy::config::core::v3::Http2ProtocolOptions& http2Options() const override;
  const envoy::config::core::v3::Http3ProtocolOptions& http3Options() const override;
  const envoy::config::core::v3::HttpProtocolOptions& commonHttpProtocolOptions() const override;
  const absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>&
  upstreamHttpProtocolOptions() const override;
  const absl::optional<const envoy::config::core::v3::AlternateProtocolsCacheOptions>&
  alternateProtocolsCacheOptions() const override;
  const std::vector<Router::ShadowPolicyPtr>& shadowPolicies() const override;
  const Router::RetryPolicy* retryPolicy() const override;
  const Http::HashPolicy* hashPolicy() const override;

  bool matches(const Upstream::HostDescription& host) const;

private:
  Http::Http1Settings http1_settings_{};
  envoy::config::core::v3::Http3ProtocolOptions http3_options_{};
  std::vector<Router::ShadowPolicyPtr> shadow_policies_{};
  absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>
      upstream_http_protocol_options_{};
  absl::optional<const envoy::config::core::v3::AlternateProtocolsCacheOptions>
      alternate_protocols_cache_options_{};
  envoy::config::core::v3::HttpProtocolOptions http_options_;
  envoy::config::core::v3::Http2ProtocolOptions http2_options_;
  Matchers::MetadataMatcher matcher_;
};

/**
 * ProtocolOptionConfig implementation for host-specific HTTP protocol options.
 * This allows specifing per-host HTTP protocol options.
 */
class ProtocolOptionsConfigImpl : public Upstream::HostHttpProtocolOptionsConfig {
public:
  ProtocolOptionsConfigImpl(
      const envoy::extensions::upstreams::host_http::v3::HostHttpProtocolOptions& config,
      Server::Configuration::CommonFactoryContext& context);

  Upstream::HttpProtocolOptionsConfigOptConstRef
  get(const Upstream::HostDescription& host) const override;

private:
  std::vector<HttpProtocolOptionsConfigImpl> options_;
};

/**
 * ProtocolOptionsFactory that loads and returns configuration for the host-specific HTTP protocol
 * options.
 */
class ProtocolOptionsConfigFactory
    : public Server::Configuration::HostHttpProtocolOptionsConfigFactory {
public:
  ~ProtocolOptionsConfigFactory() override = default;

  Upstream::HostHttpProtocolOptionsConfigConstSharedPtr createHostHttpProtocolOptionsConfig(
      const Protobuf::Message& config,
      Server::Configuration::ProtocolOptionsFactoryContext& context) const override;

  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override;

  std::string name() const override {
    return "envoy.extensions.upstreams.host_http.v3.HostHttpProtocolOptions";
  }

  std::string category() const override { return "envoy.upstream_host_http_options"; }
};

} // namespace HostHttp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
