#pragma once

#include "envoy/extensions/filters/ai/request_info/v3/request_info.pb.h"
#include "envoy/extensions/filters/ai/request_info/v3/request_info.pb.validate.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {

class RequestInfoFilterConfigFactory
    : public HttpFilters::AiProtocolManager::AiFilterConfigFactory {
public:
  absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
  createAiFilterFactory(const Protobuf::Message& config,
                        Server::Configuration::ServerFactoryContext& context,
                        Stats::Scope& scope) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::ai::request_info::v3::RequestInfo>();
  }

  std::string name() const override { return "envoy.filters.ai.request_info"; }
};

} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
