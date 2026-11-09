#pragma once

#include "envoy/extensions/http/ai_filters/schema_validation/v3/schema_validation.pb.h"
#include "envoy/extensions/http/ai_filters/schema_validation/v3/schema_validation.pb.validate.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace SchemaValidation {

class SchemaValidationFilterConfigFactory
    : public HttpFilters::AiProtocolManager::AiFilterConfigFactory {
public:
  absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
  createAiFilterFactory(const Protobuf::Message& config,
                        Server::Configuration::ServerFactoryContext& context,
                        Stats::Scope& scope) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::http::ai_filters::schema_validation::v3::SchemaValidation>();
  }

  std::string name() const override { return "envoy.http.ai_filters.schema_validation"; }
};

} // namespace SchemaValidation
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
