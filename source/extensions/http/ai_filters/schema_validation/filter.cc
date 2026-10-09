#include "source/extensions/http/ai_filters/schema_validation/filter.h"

#include <string>
#include <utility>

#include "source/extensions/filters/http/ai_protocol_manager/llm_protocol_adapter.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_protocol_conversion.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema.h"
#include "source/extensions/http/ai_filters/schema_validation/llm_protocol_detection.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace SchemaValidation {

using HttpFilters::AiProtocolManager::AdapterRegistry;
using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiRequest;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::llmProtocolName;
using HttpFilters::AiProtocolManager::LocalReplier;
using HttpFilters::AiProtocolManager::PayloadSchema;

SchemaValidationFilterConfig::SchemaValidationFilterConfig(
    const envoy::extensions::http::ai_filters::schema_validation::v3::SchemaValidation& proto,
    Stats::Scope& scope)
    : stats_(SchemaValidationFilterStats{ALL_SCHEMA_VALIDATION_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "ai_protocol_manager.schema_validation."))}),
      default_llm_protocol_(
          HttpFilters::AiProtocolManager::protocolFromProto(proto.default_llm_protocol())),
      fail_open_(proto.fail_open()) {}

SchemaValidationFilter::SchemaValidationFilter(SchemaValidationFilterConfigSharedPtr config,
                                               const AiFilterContext& context)
    : config_(std::move(config)), context_(context) {}

absl::Status SchemaValidationFilter::decodeSync(AiRequest& request, LocalReplier reply_locally) {
  request.setProtocol(resolveLlmProtocol(request));
  const PayloadSchema* schema = AdapterRegistry::get(request.protocol()).schema();
  if (schema == nullptr) {
    config_->stats().skipped_.inc();
    return absl::OkStatus();
  }

  // TODO(penguingao): validate as the parser streams, rejecting an invalid field before the
  // payload ends.
  const absl::Status status = schema->validateRequest(request.request_index());
  if (status.ok()) {
    config_->stats().valid_.inc();
    return absl::OkStatus();
  }
  config_->stats().invalid_.inc();
  ENVOY_LOG(debug, "schema_validation: invalid {} payload: {}", llmProtocolName(request.protocol()),
            status.message());
  if (!config_->failOpen()) {
    std::move(reply_locally)(Http::Code::BadRequest, std::string(status.message()));
  }
  return absl::OkStatus();
}

LLMProtocol SchemaValidationFilter::resolveLlmProtocol(const AiRequest& request) const {
  if (request.protocol() != LLMProtocol::Unspecified) {
    return request.protocol();
  }
  if (config_->defaultLlmProtocol() != LLMProtocol::Unspecified) {
    return config_->defaultLlmProtocol();
  }
  LLMProtocol detected = detectFromHeaders(context_.request_headers);
  if (detected == LLMProtocol::Unspecified) {
    detected = detectFromPayload(request.json());
  }
  if (detected != LLMProtocol::Unspecified) {
    config_->stats().llm_protocol_detected_.inc();
  }
  return detected;
}

} // namespace SchemaValidation
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
