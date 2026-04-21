#include "source/extensions/upstreams/host_http/config.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace HostHttp {

HttpProtocolOptionsConfigImpl::HttpProtocolOptionsConfigImpl(
    const envoy::extensions::upstreams::host_http::v3::HostHttpProtocolOptions::HostOptions&
        options,
    Server::Configuration::CommonFactoryContext& context)
    : http_options_(options.http_protocol_options()),
      http2_options_(options.http2_protocol_options()),
      matcher_(Matchers::MetadataMatcher(options.host_metadata_match(), context)) {}

const Http::Http1Settings& HttpProtocolOptionsConfigImpl::http1Settings() const {
  return http1_settings_;
}

const envoy::config::core::v3::Http2ProtocolOptions&
HttpProtocolOptionsConfigImpl::http2Options() const {
  return http2_options_;
}

const envoy::config::core::v3::Http3ProtocolOptions&
HttpProtocolOptionsConfigImpl::http3Options() const {
  return http3_options_;
}

const envoy::config::core::v3::HttpProtocolOptions&
HttpProtocolOptionsConfigImpl::commonHttpProtocolOptions() const {
  return http_options_;
}

const absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>&
HttpProtocolOptionsConfigImpl::upstreamHttpProtocolOptions() const {
  return upstream_http_protocol_options_;
}

const absl::optional<const envoy::config::core::v3::AlternateProtocolsCacheOptions>&
HttpProtocolOptionsConfigImpl::alternateProtocolsCacheOptions() const {
  return alternate_protocols_cache_options_;
}

const std::vector<Router::ShadowPolicyPtr>& HttpProtocolOptionsConfigImpl::shadowPolicies() const {
  return shadow_policies_;
}

const Router::RetryPolicy* HttpProtocolOptionsConfigImpl::retryPolicy() const { return nullptr; }

const Http::HashPolicy* HttpProtocolOptionsConfigImpl::hashPolicy() const { return nullptr; }

bool HttpProtocolOptionsConfigImpl::matches(const Upstream::HostDescription& host) const {
  if (!host.metadata()) {
    return false;
  }
  return matcher_.match(*host.metadata());
}

ProtocolOptionsConfigImpl::ProtocolOptionsConfigImpl(
    const envoy::extensions::upstreams::host_http::v3::HostHttpProtocolOptions& config,
    Server::Configuration::CommonFactoryContext& context) {
  for (const auto& option : config.host_options()) {
    // If matcher is not configured this option will never match, so we can as well skip it.
    if (!option.has_host_metadata_match()) {
      continue;
    }
    options_.emplace_back(option, context);
  }
}

Upstream::HttpProtocolOptionsConfigOptConstRef
ProtocolOptionsConfigImpl::get(const Upstream::HostDescription& host) const {
  for (const auto& option : options_) {
    if (option.matches(host)) {
      return option;
    }
  }
  return absl::nullopt;
}

Upstream::HostHttpProtocolOptionsConfigConstSharedPtr
ProtocolOptionsConfigFactory::createHostHttpProtocolOptionsConfig(
    const Protobuf::Message& config,
    Server::Configuration::ProtocolOptionsFactoryContext& context) const {
  const auto& conf = MessageUtil::downcastAndValidate<
      const envoy::extensions::upstreams::host_http::v3::HostHttpProtocolOptions&>(
      config, context.messageValidationVisitor());
  return std::make_shared<const ProtocolOptionsConfigImpl>(conf, context.serverFactoryContext());
}

ProtobufTypes::MessagePtr ProtocolOptionsConfigFactory::createEmptyProtocolOptionsProto() {
  return std::make_unique<envoy::extensions::upstreams::host_http::v3::HostHttpProtocolOptions>();
}

REGISTER_FACTORY(ProtocolOptionsConfigFactory,
                 Server::Configuration::HostHttpProtocolOptionsConfigFactory);

} // namespace HostHttp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
