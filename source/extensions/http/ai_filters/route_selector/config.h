#pragma once

#include "envoy/extensions/http/ai_filters/route_selector/v3/route_selector.pb.h"
#include "envoy/extensions/http/ai_filters/route_selector/v3/route_selector.pb.validate.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RouteSelector {

class RouteSelectorFilterConfigFactory
    : public HttpFilters::AiProtocolManager::AiFilterConfigFactory {
public:
  absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
  createAiFilterFactory(const Protobuf::Message& config,
                        Server::Configuration::ServerFactoryContext& context,
                        Stats::Scope& scope) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::http::ai_filters::route_selector::v3::RouteSelector>();
  }

  std::string name() const override { return "envoy.http.ai_filters.route_selector"; }
};

} // namespace RouteSelector
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
