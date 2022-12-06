#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <string>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/message_validator_impl.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {

class ProtocolOptionsConfigImpl : public Upstream::ProtocolOptionsConfig {
public:
  ProtocolOptionsConfigImpl(
      const envoy::extensions::upstreams::http::v3::HttpProtocolOptions& options,
      ProtobufMessage::ValidationVisitor& validation_visitor);
  // Constructor for legacy (deprecated) config.
  ProtocolOptionsConfigImpl(
      const envoy::config::core::v3::Http1ProtocolOptions& http1_settings,
      const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
      const envoy::config::core::v3::HttpProtocolOptions& common_options,
      const absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions> upstream_options,
      bool use_downstream_protocol, bool use_http2,
      ProtobufMessage::ValidationVisitor& validation_visitor);

  // Given the supplied cluster config, and protocol options configuration,
  // returns a unit64_t representing the enabled Upstream::ClusterInfo::Features.
  static uint64_t parseFeatures(const envoy::config::cluster::v3::Cluster& config,
                                const ProtocolOptionsConfigImpl& options);

  const Envoy::Http::Http1Settings http1_settings_;
  const envoy::config::core::v3::Http2ProtocolOptions http2_options_;
  const envoy::config::core::v3::Http3ProtocolOptions http3_options_{};
  const envoy::config::core::v3::HttpProtocolOptions common_http_protocol_options_;
  const absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>
      upstream_http_protocol_options_;

  bool use_downstream_protocol_{};
  bool use_http2_{};
  bool use_http3_{};
  bool use_alpn_{};
  using FiltersList = Protobuf::RepeatedPtrField<
      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter>;
  FiltersList http_filters_;
  absl::optional<envoy::config::core::v3::AlternateProtocolsCacheOptions>
      alternate_protocol_cache_options_;
};

class ProtocolOptionsConfigFactory : public Server::Configuration::ProtocolOptionsFactory {
public:
  Upstream::ProtocolOptionsConfigConstSharedPtr createProtocolOptionsConfig(
      const Protobuf::Message& config,
      Server::Configuration::ProtocolOptionsFactoryContext& context) override {
    const auto& typed_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::upstreams::http::v3::HttpProtocolOptions&>(
        config, context.messageValidationVisitor());
    return std::make_shared<ProtocolOptionsConfigImpl>(typed_config,
                                                       context.messageValidationVisitor());
  }
  std::string category() const override { return "envoy.upstream_options"; }
  std::string name() const override {
    return "envoy.extensions.upstreams.http.v3.HttpProtocolOptions";
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::upstreams::http::v3::HttpProtocolOptions>();
  }
  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override {
    return std::make_unique<envoy::extensions::upstreams::http::v3::HttpProtocolOptions>();
  }
};

DECLARE_FACTORY(ProtocolOptionsConfigFactory);

} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
