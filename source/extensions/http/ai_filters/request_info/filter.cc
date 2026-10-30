#include "source/extensions/http/ai_filters/request_info/filter.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

#include "envoy/data/ai/v3/request_info.pb.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/protobuf/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_request.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_protocol_conversion.h"
#include "source/extensions/http/ai_filters/request_info/extractor.h"

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {

using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiRequest;
using HttpFilters::AiProtocolManager::LocalReplier;
namespace FilterStateKeys = HttpFilters::AiProtocolManager::FilterStateKeys;

namespace {

constexpr absl::string_view DefaultMetadataNamespace{"envoy.ai.request_info"};

envoy::data::ai::v3::RequestInfo toProto(const RequestAttributes& attrs) {
  envoy::data::ai::v3::RequestInfo typed;
  typed.set_input_llm_protocol(HttpFilters::AiProtocolManager::protocolToProto(attrs.llm_protocol));
  typed.set_model(attrs.model);
  if (attrs.stream.has_value()) {
    typed.mutable_stream()->set_value(attrs.stream.value());
  }
  if (attrs.max_output_tokens.has_value()) {
    typed.mutable_max_output_tokens()->set_value(attrs.max_output_tokens.value());
  }
  if (attrs.message_count.has_value()) {
    typed.mutable_message_count()->set_value(attrs.message_count.value());
  }
  if (attrs.tool_count.has_value()) {
    typed.mutable_tool_count()->set_value(attrs.tool_count.value());
  }
  return typed;
}

// The payload is bounded by the manager's buffer limits and the ratio by 1.0, so the product
// stays well inside the exactly-representable range.
uint64_t estimateInputTokens(uint64_t payload_bytes, double tokens_per_byte) {
  return static_cast<uint64_t>(std::ceil(static_cast<double>(payload_bytes) * tokens_per_byte));
}

std::optional<ExtractionConfig>
compileExtraction(const envoy::extensions::http::ai_filters::request_info::v3::RequestInfo& proto,
                  const std::string& default_namespace) {
  if (!proto.has_extraction()) {
    return std::nullopt;
  }
  const auto& extraction = proto.extraction();
  ExtractionConfig config;
  config.metadata_namespace =
      extraction.metadata_namespace().empty() ? default_namespace : extraction.metadata_namespace();
  config.rules.reserve(extraction.rules().size());
  for (const auto& rule_proto : extraction.rules()) {
    ExtractionRule rule;
    rule.path.assign(rule_proto.path().begin(), rule_proto.path().end());
    rule.key = rule_proto.key().empty() ? absl::StrJoin(rule.path, ".") : rule_proto.key();
    if (rule_proto.has_on_missing()) {
      rule.on_missing = rule_proto.on_missing();
    }
    config.rules.push_back(std::move(rule));
  }
  const auto& limits = extraction.limits();
  config.limits.max_value_bytes =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(limits, max_value_bytes, ExtractionLimits{}.max_value_bytes);
  config.limits.max_total_bytes =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(limits, max_total_bytes, ExtractionLimits{}.max_total_bytes);
  config.limits.max_depth =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(limits, max_depth, ExtractionLimits{}.max_depth);
  config.limits.max_string_bytes = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      limits, max_string_bytes, ExtractionLimits{}.max_string_bytes);
  return config;
}

} // namespace

RequestInfoFilterConfig::RequestInfoFilterConfig(
    const envoy::extensions::http::ai_filters::request_info::v3::RequestInfo& proto,
    Stats::Scope& scope)
    : stats_(RequestInfoFilterStats{ALL_REQUEST_INFO_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "ai_protocol_manager.request_info."))}),
      metadata_namespace_(proto.metadata_namespace().empty() ? std::string(DefaultMetadataNamespace)
                                                             : proto.metadata_namespace()),
      tokens_per_byte_(proto.has_token_estimation()
                           ? std::make_optional(proto.token_estimation().tokens_per_byte())
                           : std::nullopt),
      extraction_(compileExtraction(proto, metadata_namespace_)) {}

RequestInfoFilter::RequestInfoFilter(RequestInfoFilterConfigSharedPtr config,
                                     const AiFilterContext& context)
    : config_(std::move(config)), context_(context) {}

absl::Status RequestInfoFilter::decodeSync(AiRequest& request, LocalReplier) {
  publish(request);
  return absl::OkStatus();
}

void RequestInfoFilter::publish(const AiRequest& request) {
  StreamInfo::StreamInfo& stream_info = context_.stream_info;
  if (stream_info.dynamicMetadata().typed_filter_metadata().contains(
          config_->metadataNamespace())) {
    ENVOY_LOG(debug, "request_info: namespace {} already published; skipping",
              config_->metadataNamespace());
    config_->stats().duplicate_.inc();
    return;
  }

  const RequestAttributes attrs = extractRequestAttributes(request.protocol(), request.json(),
                                                           context_.request_headers.getPathValue());
  envoy::data::ai::v3::RequestInfo record = toProto(attrs);
  if (config_->tokensPerByte().has_value()) {
    record.mutable_estimated_input_tokens()->set_value(
        estimateInputTokens(context_.request_payload_bytes, *config_->tokensPerByte()));
  }
  Protobuf::Any typed_any;
  MessageUtil::packFrom(typed_any, record);
  stream_info.setDynamicTypedMetadata(config_->metadataNamespace(), typed_any);
  storeModel(attrs.model);
  const bool extraction_incomplete = extract(request);

  config_->stats().published_.inc();
  if (attrs.malformed || extraction_incomplete) {
    config_->stats().partial_.inc();
  }
  ENVOY_LOG(trace, "request_info: published to namespace {}", config_->metadataNamespace());
}

bool RequestInfoFilter::extract(const AiRequest& request) {
  if (!config_->extraction().has_value()) {
    return false;
  }
  const ExtractionConfig& extraction = *config_->extraction();
  StreamInfo::StreamInfo& stream_info = context_.stream_info;
  const auto& untyped = stream_info.dynamicMetadata().filter_metadata();
  const auto existing_it = untyped.find(extraction.metadata_namespace);
  const Protobuf::Struct* existing = existing_it == untyped.end() ? nullptr : &existing_it->second;

  Protobuf::Struct out;
  const ExtractionOutcome outcome =
      extractConfiguredValues(request.json(), extraction.rules, extraction.limits, existing, out);
  if (!out.fields().empty()) {
    // Merges into the namespace; `out` holds only keys no earlier writer set.
    stream_info.setDynamicMetadata(extraction.metadata_namespace, out);
  }
  if (outcome.dropped > 0) {
    config_->stats().extraction_dropped_.add(outcome.dropped);
    ENVOY_LOG(debug, "request_info: {} extracted value(s) over the configured limits dropped",
              outcome.dropped);
  }
  if (outcome.unusable > 0) {
    config_->stats().extraction_unusable_.add(outcome.unusable);
    ENVOY_LOG(debug, "request_info: {} extracted value(s) not representable as metadata dropped",
              outcome.unusable);
  }
  ENVOY_LOG(trace, "request_info: extracted {} value(s) into namespace {}", outcome.written,
            extraction.metadata_namespace);
  return outcome.dropped > 0 || outcome.unusable > 0;
}

void RequestInfoFilter::storeModel(absl::string_view model) {
  const StreamInfo::FilterStateSharedPtr& filter_state = context_.stream_info.filterState();
  // A filter ahead may have set the object with another life span, which setData rejects.
  if (model.empty() || filter_state->hasDataWithName(FilterStateKeys::ModelRequest)) {
    return;
  }
  filter_state->setData(FilterStateKeys::ModelRequest,
                        std::make_shared<Router::StringAccessorImpl>(model),
                        StreamInfo::FilterState::LifeSpan::FilterChain);
}

} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
