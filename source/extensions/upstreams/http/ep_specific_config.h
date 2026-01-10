#pragma once

#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.validate.h"
#include "envoy/server/filter_config.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {

/**
 * Config implementation for EpSpecificHttpProtocolOptions.
 * This allows per-endpoint HTTP protocol options to be specified.
 */
class EpSpecificProtocolOptionsConfigImpl : public Upstream::ProtocolOptionsConfig {
public:
  EpSpecificProtocolOptionsConfigImpl(
      const envoy::extensions::upstreams::http::v3::EpSpecificHttpProtocolOptions& config)
      : config_(config) {}

  const envoy::extensions::upstreams::http::v3::EpSpecificHttpProtocolOptions& config() const {
    return config_;
  }

private:
  const envoy::extensions::upstreams::http::v3::EpSpecificHttpProtocolOptions config_;
};

/**
 * Factory for EpSpecificHttpProtocolOptions protocol options config.
 */
class EpSpecificProtocolOptionsConfigFactory
    : public Server::Configuration::ProtocolOptionsFactory {
public:
  absl::StatusOr<Upstream::ProtocolOptionsConfigConstSharedPtr> createProtocolOptionsConfig(
      const Protobuf::Message& config,
      Server::Configuration::ProtocolOptionsFactoryContext& context) override {
    const auto& typed_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::upstreams::http::v3::EpSpecificHttpProtocolOptions&>(
        config, context.messageValidationVisitor());
    return std::make_shared<EpSpecificProtocolOptionsConfigImpl>(typed_config);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::upstreams::http::v3::EpSpecificHttpProtocolOptions>();
  }

  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override {
    return std::make_unique<envoy::extensions::upstreams::http::v3::EpSpecificHttpProtocolOptions>();
  }

  std::string category() const override { return "envoy.upstream_options"; }
  
  std::string name() const override {
    return "envoy.extensions.upstreams.http.v3.EpSpecificHttpProtocolOptions";
  }
};

} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
