#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include <memory>

#include "envoy/data/ai/v3/token_usage.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

constexpr absl::string_view DefaultTokenUsageNamespace{"envoy.ai.token_usage"};
constexpr absl::string_view SseContentType{"text/event-stream"};
constexpr absl::string_view JsonContentType{"application/json"};
// Sized so that ordinary OpenAI Responses API terminal lifecycle events --
// which embed the complete response object, generated output included -- are
// extractable by default; see the proto for the rationale.
constexpr uint32_t DefaultMaxEventSize = 1024 * 1024;
constexpr uint32_t DefaultMaxInspectedBodySize = 4 * 1024 * 1024;
// Far above ordinary streams (hundreds to low thousands of events) while
// bounding the parse work a lifetime event flood can extract; see the proto.
constexpr uint32_t DefaultMaxParsedEvents = 65536;

ApiFormat formatFromProto(
    envoy::extensions::filters::http::ai_protocol_manager::v3::BuiltinTokenUsageExtractor::ApiFormat
        format) {
  using ProtoFormat =
      envoy::extensions::filters::http::ai_protocol_manager::v3::BuiltinTokenUsageExtractor;
  switch (format) {
  case ProtoFormat::OPENAI:
    return ApiFormat::OpenAi;
  case ProtoFormat::ANTHROPIC:
    return ApiFormat::Anthropic;
  case ProtoFormat::GEMINI:
    return ApiFormat::Gemini;
  default:
    return ApiFormat::Unknown; // AUTO_DETECT
  }
}

// HTTP Content-Type is case-insensitive and may carry parameters
// (`application/json; charset=utf-8`).
bool contentTypeMatches(absl::string_view content_type, absl::string_view expected) {
  const absl::string_view normalized = StringUtil::trim(StringUtil::cropRight(content_type, ";"));
  return absl::EqualsIgnoreCase(normalized, expected);
}

// Extraction requires an unencoded body: every entry and comma-separated
// coding of the (repeatable) Content-Encoding header must be `identity`.
bool contentEncodingIsIdentity(const Http::ResponseHeaderMap& headers) {
  const auto entries = headers.get(Http::CustomHeaders::get().ContentEncoding);
  for (size_t i = 0; i < entries.size(); ++i) {
    // keep_empty_string=true rejects malformed values like `identity,,`.
    for (const absl::string_view coding : StringUtil::splitToken(
             entries[i]->value().getStringView(), ",", /*keep_empty_string=*/true)) {
      if (!absl::EqualsIgnoreCase(StringUtil::trim(coding), "identity")) {
        return false;
      }
    }
  }
  return true;
}

void setCount(Protobuf::Struct& metadata, absl::string_view key,
              const std::optional<uint64_t>& value) {
  if (value.has_value()) {
    (*metadata.mutable_fields())[std::string(key)].set_number_value(
        static_cast<double>(value.value()));
  }
}

envoy::data::ai::v3::TokenUsage::ApiFormat typedApiFormat(ApiFormat format) {
  using TypedUsage = envoy::data::ai::v3::TokenUsage;
  switch (format) {
  case ApiFormat::OpenAi:
    return TypedUsage::OPENAI;
  case ApiFormat::Anthropic:
    return TypedUsage::ANTHROPIC;
  case ApiFormat::Gemini:
    return TypedUsage::GEMINI;
  case ApiFormat::Unknown:
    break;
  }
  return TypedUsage::API_FORMAT_UNSPECIFIED;
}

// Converts the finalized accumulator once into the authoritative typed
// record (envoy.data.ai.v3.TokenUsage); the untyped Struct is projected
// from it, never built independently.
envoy::data::ai::v3::TokenUsage typedUsage(const TokenUsage& usage, bool degraded) {
  envoy::data::ai::v3::TokenUsage typed;
  typed.set_api_format(typedApiFormat(usage.api_format));
  if (!usage.model.empty()) {
    typed.set_model(usage.model);
  }
  const auto set = [](const std::optional<uint64_t>& value, auto setter) {
    if (value.has_value()) {
      setter(value.value());
    }
  };
  set(usage.input_tokens, [&](uint64_t v) { typed.mutable_input_tokens()->set_value(v); });
  set(usage.output_tokens, [&](uint64_t v) { typed.mutable_output_tokens()->set_value(v); });
  set(usage.total_tokens, [&](uint64_t v) { typed.mutable_total_tokens()->set_value(v); });
  set(usage.cached_input_tokens,
      [&](uint64_t v) { typed.mutable_cached_input_tokens()->set_value(v); });
  set(usage.cache_creation_input_tokens,
      [&](uint64_t v) { typed.mutable_cache_creation_input_tokens()->set_value(v); });
  set(usage.tool_use_input_tokens,
      [&](uint64_t v) { typed.mutable_tool_use_input_tokens()->set_value(v); });
  set(usage.reasoning_tokens, [&](uint64_t v) { typed.mutable_reasoning_tokens()->set_value(v); });
  set(usage.reported_total_tokens,
      [&](uint64_t v) { typed.mutable_reported_total_tokens()->set_value(v); });
  typed.set_extraction_status(degraded ? envoy::data::ai::v3::TokenUsage::PARTIAL
                                       : envoy::data::ai::v3::TokenUsage::COMPLETE);
  return typed;
}

// The untyped Struct projection: identical keys and values to the historic
// Struct interface, for consumers that only read untyped metadata
// (%DYNAMIC_METADATA%, CEL). Counts are double-backed here; the typed record
// carries full uint64 precision.
Protobuf::Struct structProjection(const envoy::data::ai::v3::TokenUsage& typed) {
  using TypedUsage = envoy::data::ai::v3::TokenUsage;
  Protobuf::Struct metadata;
  auto& fields = *metadata.mutable_fields();
  absl::string_view format_name = "unknown";
  switch (typed.api_format()) {
  case TypedUsage::OPENAI:
    format_name = "openai";
    break;
  case TypedUsage::ANTHROPIC:
    format_name = "anthropic";
    break;
  case TypedUsage::GEMINI:
    format_name = "gemini";
    break;
  default:
    break;
  }
  fields["api_format"].set_string_value(std::string(format_name));
  if (!typed.model().empty()) {
    fields["model"].set_string_value(typed.model());
  }
  const auto set_count = [&metadata](absl::string_view key, bool present, uint64_t value) {
    if (present) {
      setCount(metadata, key, value);
    }
  };
  set_count("input_tokens", typed.has_input_tokens(), typed.input_tokens().value());
  set_count("output_tokens", typed.has_output_tokens(), typed.output_tokens().value());
  set_count("total_tokens", typed.has_total_tokens(), typed.total_tokens().value());
  set_count("cached_input_tokens", typed.has_cached_input_tokens(),
            typed.cached_input_tokens().value());
  set_count("cache_creation_input_tokens", typed.has_cache_creation_input_tokens(),
            typed.cache_creation_input_tokens().value());
  set_count("tool_use_input_tokens", typed.has_tool_use_input_tokens(),
            typed.tool_use_input_tokens().value());
  set_count("reasoning_tokens", typed.has_reasoning_tokens(), typed.reasoning_tokens().value());
  set_count("reported_total_tokens", typed.has_reported_total_tokens(),
            typed.reported_total_tokens().value());
  fields["extraction_status"].set_string_value(
      typed.extraction_status() == TypedUsage::PARTIAL ? "partial" : "complete");
  return metadata;
}

} // namespace

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager& config,
    Stats::Scope& scope)
    : stats_(AiProtocolManagerStats{
          ALL_AI_PROTOCOL_MANAGER_STATS(POOL_COUNTER_PREFIX(scope, "ai_protocol_manager."))}),
      response_handling_enabled_(config.has_response_handling()),
      request_offload_enabled_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.request_handling(),
                                                               payload_offload_enabled, true)),
      api_format_(formatFromProto(config.response_handling().token_usage().api_format())),
      metadata_namespace_(config.response_handling().metadata_namespace().empty()
                              ? std::string(DefaultTokenUsageNamespace)
                              : config.response_handling().metadata_namespace()),
      max_event_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.response_handling(), max_event_size,
                                                      DefaultMaxEventSize)),
      max_inspected_body_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          config.response_handling(), max_inspected_body_size, DefaultMaxInspectedBodySize)),
      max_parsed_events_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          config.response_handling(), max_parsed_events, DefaultMaxParsedEvents)) {}

void AiProtocolManagerFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  PassThroughFilter::setDecoderFilterCallbacks(callbacks);
  // Response-only installation: the decode path is a pure passthrough and
  // needs no manager (no watermark subscription, no external buffer).
  if (config_ != nullptr && !config_->requestOffloadEnabled()) {
    return;
  }
  // Construct the decode-path manager. Its constructor subscribes to upstream
  // watermarks (via the bridge) so replay can be paced against upstream
  // back-pressure; subscribing may immediately deliver high-watermark callbacks
  // if the upstream is already backed up.
  decode_manager_ = std::make_unique<BufferManager>(
      buffer_factory_, std::make_unique<DecoderFilterChainBridge>(*decoder_callbacks_));
}

void AiProtocolManagerFilter::onDestroy() {
  if (decode_manager_ != nullptr) {
    // Detach the manager (releases the external buffer and unsubscribes from
    // watermarks) but do NOT free it here. onDestroy() can run synchronously while
    // the manager is mid-replay -- a downstream filter answering an injected frame
    // with a local reply reaches destroyFilters() on this very stack -- and freeing
    // the manager then would pull it out from under its own injectData()/read()
    // reentrancy. The manager is owned by unique_ptr and freed when this filter is
    // (deferred-)destroyed, by which point the replay stack has unwound. This honors
    // BufferManager's onDestroy()-before-destruction contract (see buffer_manager.h).
    decode_manager_->onDestroy();
  }
}

Http::FilterHeadersStatus AiProtocolManagerFilter::decodeHeaders(Http::RequestHeaderMap&,
                                                                 bool end_stream) {
  // Response-only installation: requests stream through untouched.
  if (decode_manager_ == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }
  // A headers-only request carries no payload to inspect, so there is nothing to
  // hold the chain for: let the headers flow. (Pausing here would also deadlock,
  // since no body would ever arrive to drive the replay that releases them.)
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }

  // A body follows: pin the headers at this filter so the subsequent routing and
  // admission filters do not act on them until the payload has been offloaded.
  // decodeData() still fires on this filter while iteration is stopped here, so
  // the BufferManager keeps offloading; the held headers are released when replay
  // injects the first body frame (or, for an empty/trailer-only body, when the
  // BufferManager continues iteration).
  ENVOY_LOG(trace, "ai_protocol_manager: holding headers until payload is offloaded");
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus AiProtocolManagerFilter::decodeData(Buffer::Instance& data,
                                                           bool end_stream) {
  if (decode_manager_ == nullptr) {
    return Http::FilterDataStatus::Continue;
  }
  decode_manager_->onData(data);
  if (end_stream) {
    // The full body has been offloaded. The filter owns replay and end-of-stream:
    // a future change will assemble and inspect the request here (and may replay
    // sub-ranges); for now replay the whole body, then emit the terminal frame.
    decode_manager_->endStream();
    decode_manager_->replay(0, decode_manager_->length(), [this]() {
      // Terminate the stream with an empty end_stream data frame after the replayed
      // body (also releases the held headers when the body was empty).
      Buffer::OwnedImpl end_marker;
      decoder_callbacks_->injectDecodedDataToFilterChain(end_marker, /*end_stream=*/true);
    });
  }
  // Hold the chain here; the BufferManager replays the payload once told to.
  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus AiProtocolManagerFilter::decodeTrailers(Http::RequestTrailerMap&) {
  if (decode_manager_ == nullptr) {
    return Http::FilterTrailersStatus::Continue;
  }
  // A trailer-only request (no body) has nothing to replay; let the trailers flow.
  if (decode_manager_->empty()) {
    return Http::FilterTrailersStatus::Continue;
  }
  // The body ended without end_stream on a data frame; the trailers carry it.
  decode_manager_->endStream();
  decode_manager_->replay(0, decode_manager_->length(), [this]() {
    // Body fully replayed; release the held trailers (they carry END_STREAM) so
    // they follow the body in order.
    decoder_callbacks_->continueDecoding();
  });
  // Hold the trailers behind the replayed body until the replay-done callback
  // above releases them.
  return Http::FilterTrailersStatus::StopIteration;
}

Http::FilterHeadersStatus AiProtocolManagerFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                                 bool end_stream) {
  // The encode path is observe-only: headers are never held and the handler
  // selection below can only make the filter inert, never affect the response.
  if (config_ == nullptr || !config_->responseHandlingEnabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Only successful responses carry usage; errors and local replies pass through
  // uninspected.
  const auto status = Http::Utility::getResponseStatusOrNullopt(headers);
  if (!status.has_value() || status.value() < 200 || status.value() >= 300) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Content-type selection first: only responses that would otherwise be
  // inspected can count an encoding skip.
  const absl::string_view content_type = headers.getContentTypeValue();
  const bool is_sse = contentTypeMatches(content_type, SseContentType);
  const bool is_json = !is_sse && contentTypeMatches(content_type, JsonContentType);
  if (!is_sse && !is_json) {
    return Http::FilterHeadersStatus::Continue;
  }

  // A compressed body cannot be inspected here: unless the body is decompressed
  // before this filter on the encode path, extraction is skipped (the response
  // is unaffected either way).
  if (!contentEncodingIsIdentity(headers)) {
    config_->stats().unsupported_content_encoding_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  // An eligible headers-only response carries no usage: counted like an
  // empty terminal body, independent of codec framing.
  if (end_stream) {
    config_->stats().token_usage_missing_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  // A JSON body already known (from content-length) to exceed the cap skips
  // extraction up front; the onData() cap stays authoritative for absent or
  // wrong lengths.
  if (is_json) {
    uint64_t content_length = 0;
    if (absl::SimpleAtoi(headers.getContentLengthValue(), &content_length) &&
        content_length > config_->maxInspectedBodySize()) {
      ENVOY_LOG(debug,
                "ai_protocol_manager: content-length {} exceeds max_inspected_body_size ({}), "
                "skipping token extraction",
                content_length, config_->maxInspectedBodySize());
      config_->stats().response_body_too_large_.inc();
      return Http::FilterHeadersStatus::Continue;
    }
  }

  // Observation buffers charge the stream's memory account when tracking is
  // enabled; in an upstream installation the decoder callbacks resolve to the
  // downstream stream's account.
  const Buffer::BufferMemoryAccountSharedPtr account =
      decoder_callbacks_ != nullptr ? decoder_callbacks_->account() : nullptr;
  if (is_sse) {
    response_handler_ =
        std::make_unique<SseResponseHandler>(config_->apiFormat(), config_->maxEventSize(),
                                             config_->maxParsedEvents(), config_->stats(), account);
  } else {
    response_handler_ = std::make_unique<JsonResponseHandler>(
        config_->apiFormat(), config_->maxInspectedBodySize(), config_->stats(), account);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus AiProtocolManagerFilter::encodeData(Buffer::Instance& data,
                                                           bool end_stream) {
  if (response_handler_ != nullptr && !response_finalized_) {
    response_handler_->onData(data);
    if (end_stream) {
      response_handler_->onEndStream();
      finalizeResponseHandling();
    }
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus AiProtocolManagerFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  if (response_handler_ != nullptr && !response_finalized_) {
    // Trailers end the body: the handler must observe end-of-stream (parsing a
    // buffered JSON body, resolving a pending SSE terminator) before the
    // result is finalized.
    response_handler_->onEndStream();
    finalizeResponseHandling();
  }
  return Http::FilterTrailersStatus::Continue;
}

void AiProtocolManagerFilter::finalizeResponseHandling() {
  response_finalized_ = true;

  TokenUsage usage = response_handler_->usage();
  usage.finalize();
  const bool degraded = response_handler_->degraded() || usage.canonicalizationOverflow();
  if (!usage.hasAny() && !degraded) {
    // Legitimately absent usage: e.g. an OpenAI stream without
    // `stream_options.include_usage`, or an unrecognized response shape.
    config_->stats().token_usage_missing_.inc();
    return;
  }

  // setDynamicMetadata *merges* Structs per namespace, so two publications
  // for one stream (both-placement installs) would blend two observations
  // into one hybrid record. First writer wins: the upstream instance
  // publishes first, later instances skip.
  const auto& existing_metadata =
      encoder_callbacks_->streamInfo().dynamicMetadata().filter_metadata();
  if (existing_metadata.contains(config_->metadataNamespace())) {
    ENVOY_LOG(debug,
              "ai_protocol_manager: namespace {} already published for this stream "
              "(both-placement installation); skipping duplicate publication",
              config_->metadataNamespace());
    config_->stats().token_usage_duplicate_.inc();
    return;
  }

  // Convert the finalized accumulator once into the authoritative typed
  // record; both the typed publication and the untyped Struct projection come
  // from it. When extraction failed outright the record is status-only
  // (api_format/model/extraction_status, no counts), letting consumers
  // distinguish "failed to extract" from "no usage supplied".
  const envoy::data::ai::v3::TokenUsage typed = typedUsage(usage, degraded);
  // In both roles streamInfo() resolves to the downstream request's
  // StreamInfo. Under retries/hedging only the router-selected attempt
  // streams to a clean end of stream, so only the winner reaches this write.
  // Typed metadata is the authoritative record; the Struct projection under
  // the same namespace serves %DYNAMIC_METADATA%/CEL, which read untyped
  // metadata only, so the same-key precedence rule never bites in practice.
  Protobuf::Any typed_any;
  MessageUtil::packFrom(typed_any, typed);
  encoder_callbacks_->streamInfo().setDynamicTypedMetadata(config_->metadataNamespace(), typed_any);
  encoder_callbacks_->streamInfo().setDynamicMetadata(config_->metadataNamespace(),
                                                      structProjection(typed));

  if (!usage.hasAny()) {
    config_->stats().token_usage_partial_.inc();
    ENVOY_LOG(trace, "ai_protocol_manager: status-only (partial) record published to namespace {}",
              config_->metadataNamespace());
    return;
  }
  if (degraded) {
    config_->stats().token_usage_partial_.inc();
  }
  if (usage.reported_total_tokens.has_value()) {
    config_->stats().token_usage_total_mismatch_.inc();
  }
  config_->stats().token_usage_found_.inc();
  ENVOY_LOG(trace, "ai_protocol_manager: token usage published to namespace {}",
            config_->metadataNamespace());
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
