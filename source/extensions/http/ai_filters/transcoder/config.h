#pragma once

#include "envoy/extensions/http/ai_filters/transcoder/v3/transcoder.pb.h"
#include "envoy/extensions/http/ai_filters/transcoder/v3/transcoder.pb.validate.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Transcoder {

class TranscoderFilterConfigFactory : public HttpFilters::AiProtocolManager::AiFilterConfigFactory {
public:
  absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
  createAiFilterFactory(const Protobuf::Message& config,
                        Server::Configuration::ServerFactoryContext& context,
                        Stats::Scope& scope) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::http::ai_filters::transcoder::v3::Transcoder>();
  }

  std::string name() const override { return "envoy.http.ai_filters.transcoder"; }
};

} // namespace Transcoder
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
