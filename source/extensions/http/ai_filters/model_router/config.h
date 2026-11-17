#pragma once

#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.h"
#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.validate.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

class ModelRouterFilterConfigFactory
    : public HttpFilters::AiProtocolManager::AiFilterConfigFactory {
public:
  absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
  createAiFilterFactory(const Protobuf::Message& config,
                        Server::Configuration::ServerFactoryContext& context,
                        Stats::Scope& scope) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::http::ai_filters::model_router::v3::ModelRouter>();
  }

  std::string name() const override { return "envoy.http.ai_filters.model_router"; }
};

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
