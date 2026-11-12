#pragma once

#include "envoy/extensions/http/ai_filters/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/http/ai_filters/dynamic_modules/v3/dynamic_modules.pb.validate.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace DynamicModules {

class DynamicModuleAiFilterConfigFactory
    : public HttpFilters::AiProtocolManager::AiFilterConfigFactory {
public:
  absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
  createAiFilterFactory(const Protobuf::Message& config,
                        Server::Configuration::ServerFactoryContext& context,
                        Stats::Scope& scope) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::http::ai_filters::dynamic_modules::v3::DynamicModuleAiFilter>();
  }

  std::string name() const override { return "envoy.http.ai_filters.dynamic_modules"; }
};

} // namespace DynamicModules
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
