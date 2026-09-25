#include "source/extensions/http/ai_filters/transcoder/config.h"

#include <utility>

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ai_protocol_manager/transcoding_engine.h"
#include "source/extensions/http/ai_filters/transcoder/filter.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Transcoder {

using TranscoderProto = envoy::extensions::http::ai_filters::transcoder::v3::Transcoder;

absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
TranscoderFilterConfigFactory::createAiFilterFactory(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope& scope) {
  const auto& proto = MessageUtil::downcastAndValidate<const TranscoderProto&>(
      config, context.messageValidationVisitor());

  const bool has_request = proto.request_handling() == TranscoderProto::TO_IR ||
                           proto.request_handling() == TranscoderProto::FROM_IR;
  const bool has_response = proto.response_handling() == TranscoderProto::TO_IR ||
                            proto.response_handling() == TranscoderProto::FROM_IR;
  if (!has_request && !has_response) {
    return absl::InvalidArgumentError(
        "ai_filters.transcoder: at least one of `request_handling` or `response_handling` must be "
        "set");
  }

  // Built once here rather than per stream. registerPack() validates every declarative rule set
  // against its schema, so a malformed rule set becomes a config load error and Envoy refuses to
  // start, instead of surfacing as per-request failures in production.
  absl::StatusOr<HttpFilters::AiProtocolManager::TranscodingEngine> engine =
      HttpFilters::AiProtocolManager::TranscodingEngine::createDefault();
  if (!engine.ok()) {
    return engine.status();
  }

  auto filter_config =
      std::make_shared<const TranscoderFilterConfig>(proto, std::move(*engine), scope);
  return [filter_config](const HttpFilters::AiProtocolManager::AiFilterContext& stream_context)
             -> HttpFilters::AiProtocolManager::AiFilterSharedPtr {
    return std::make_shared<TranscoderFilter>(filter_config, stream_context);
  };
}

REGISTER_FACTORY(TranscoderFilterConfigFactory,
                 HttpFilters::AiProtocolManager::AiFilterConfigFactory);

} // namespace Transcoder
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
