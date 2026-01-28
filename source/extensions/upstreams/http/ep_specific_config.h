#pragma once

#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.validate.h"
#include "envoy/server/filter_config.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {

/**
 * Compiled endpoint-specific options with pre-constructed matchers.
 */
struct CompiledEpSpecificOptions {
  CompiledEpSpecificOptions(
      const envoy::extensions::upstreams::http::v3::EndpointSpecificHttpProtocolOptions::EndpointSpecificOptions&
          options,
      Server::Configuration::CommonFactoryContext& context)
      : http2_protocol_options(options.has_http2_protocol_options()
                                   ? absl::optional<envoy::config::core::v3::Http2ProtocolOptions>(
                                         options.http2_protocol_options())
                                   : absl::nullopt),
        http_protocol_options(options.has_http_protocol_options()
                                  ? absl::optional<envoy::config::core::v3::HttpProtocolOptions>(
                                        options.http_protocol_options())
                                  : absl::nullopt),
        metadata_matcher(options.has_endpoint_metadata_match()
                             ? absl::optional<Matchers::MetadataMatcher>(
                                   Matchers::MetadataMatcher(options.endpoint_metadata_match(), context))
                             : absl::nullopt) {}

  absl::optional<envoy::config::core::v3::Http2ProtocolOptions> http2_protocol_options;
  absl::optional<envoy::config::core::v3::HttpProtocolOptions> http_protocol_options;
  absl::optional<Matchers::MetadataMatcher> metadata_matcher;
};

/**
 * Config implementation for EndpointSpecificHttpProtocolOptions.
 * This allows per-endpoint HTTP protocol options to be specified.
 */
class EpSpecificProtocolOptionsConfigImpl : public Upstream::ProtocolOptionsConfig {
public:
  EpSpecificProtocolOptionsConfigImpl(
      const envoy::extensions::upstreams::http::v3::EndpointSpecificHttpProtocolOptions& config,
      Server::Configuration::CommonFactoryContext& context) {
    for (const auto& ep_option : config.endpoint_specific_options()) {
      compiled_options_.emplace_back(ep_option, context);
    }
  }

  const std::vector<CompiledEpSpecificOptions>& compiledOptions() const {
    return compiled_options_;
  }

private:
  std::vector<CompiledEpSpecificOptions> compiled_options_;
};

/**
 * Factory for EndpointSpecificHttpProtocolOptions protocol options config.
 */
class EpSpecificProtocolOptionsConfigFactory
    : public Server::Configuration::ProtocolOptionsFactory {
public:
  absl::StatusOr<Upstream::ProtocolOptionsConfigConstSharedPtr> createProtocolOptionsConfig(
      const Protobuf::Message& config,
      Server::Configuration::ProtocolOptionsFactoryContext& context) override {
    const auto& typed_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::upstreams::http::v3::EndpointSpecificHttpProtocolOptions&>(
        config, context.messageValidationVisitor());
    return std::make_shared<EpSpecificProtocolOptionsConfigImpl>(typed_config,
                                                                  context.serverFactoryContext());
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::upstreams::http::v3::EndpointSpecificHttpProtocolOptions>();
  }

  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override {
    return std::make_unique<envoy::extensions::upstreams::http::v3::EndpointSpecificHttpProtocolOptions>();
  }

  std::string category() const override { return "envoy.upstream_options"; }
  
  std::string name() const override {
    return "envoy.extensions.upstreams.http.v3.EndpointSpecificHttpProtocolOptions";
  }
};

} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
