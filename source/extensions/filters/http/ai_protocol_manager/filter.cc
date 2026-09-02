#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include <memory>

#include "envoy/data/ai/v3/token_usage.pb.h"
#include "envoy/http/codes.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ai_protocol_manager/api_protocol_adapter.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema.h"

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
// extracted by default; see the proto for the rationale.
constexpr uint32_t DefaultMaxSseEventSize = 1024 * 1024;
constexpr uint32_t DefaultMaxJsonBodySize = 4 * 1024 * 1024;
// Far above ordinary streams (hundreds to low thousands of events) while
// bounding the parse work a lifetime event flood can extract; see the proto.
constexpr uint32_t DefaultMaxParsedSseEvents = 65536;

// HTTP Content-Type is case-insensitive and may carry parameters
// (`application/json; charset=utf-8`).
bool contentTypeMatches(absl::string_view content_type, absl::string_view expected) {
  const absl::string_view normalized = StringUtil::trim(StringUtil::cropRight(content_type, ";"));
  return absl::EqualsIgnoreCase(normalized, expected);
}

// application/json or any +json structured-syntax suffix; parameters and
// case are ignored.
bool isJsonContentType(absl::string_view content_type) {
  const absl::string_view normalized = StringUtil::trim(StringUtil::cropRight(content_type, ";"));
  return absl::EqualsIgnoreCase(normalized, "application/json") ||
         absl::EndsWithIgnoreCase(normalized, "+json");
}

// Whether an unconfigured route's request can be held to end of stream.
//
// Holding the headers is only safe if the client finishes its request before it
// wants a response. A full-duplex stream -- gRPC or Connect streaming, an
// upgrade, CONNECT -- may instead wait on a response the held upstream cannot
// produce, and stall until it times out.
bool canHoldRequest(const Http::RequestHeaderMap& headers) {
  if (Grpc::Common::isGrpcRequestHeaders(headers) ||
      Grpc::Common::isConnectStreamingRequestHeaders(headers)) {
    return false;
  }
  if (Http::Utility::isUpgrade(headers) || Http::HeaderUtility::isConnect(headers)) {
    return false;
  }
  return isJsonContentType(headers.getContentTypeValue());
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

// Converts the finalized accumulator once into the authoritative typed
// record (envoy.data.ai.v3.TokenUsage). A record with no counts at all is
// status-only and publishes as FAILED.
envoy::data::ai::v3::TokenUsage typedUsage(const TokenUsage& usage, bool degraded) {
  envoy::data::ai::v3::TokenUsage typed;
  typed.set_api_protocol(protocolToProto(usage.api_protocol));
  if (!usage.model.empty()) {
    typed.set_model(usage.model);
  }
  const auto set = [](const std::optional<uint64_t>& value, auto setter) {
    if (value.has_value()) {
      setter(value.value());
    }
  };
  set(usage.input_tokens, [&typed](uint64_t v) { typed.mutable_input_tokens()->set_value(v); });
  set(usage.output_tokens, [&typed](uint64_t v) { typed.mutable_output_tokens()->set_value(v); });
  set(usage.total_tokens, [&typed](uint64_t v) { typed.mutable_total_tokens()->set_value(v); });
  set(usage.cached_input_tokens, [&typed](uint64_t v) {
    typed.mutable_input_token_details()->mutable_cached_tokens()->set_value(v);
  });
  set(usage.cache_creation_input_tokens, [&typed](uint64_t v) {
    typed.mutable_input_token_details()->mutable_cache_creation_tokens()->set_value(v);
  });
  set(usage.tool_use_input_tokens, [&typed](uint64_t v) {
    typed.mutable_input_token_details()->mutable_tool_use_tokens()->set_value(v);
  });
  set(usage.reasoning_tokens, [&typed](uint64_t v) {
    typed.mutable_output_token_details()->mutable_reasoning_tokens()->set_value(v);
  });
  set(usage.provider_total_tokens,
      [&typed](uint64_t v) { typed.mutable_provider_total_tokens()->set_value(v); });
  if (!usage.hasAny()) {
    typed.set_extraction_status(envoy::data::ai::v3::TokenUsage::FAILED);
  } else {
    typed.set_extraction_status(degraded ? envoy::data::ai::v3::TokenUsage::PARTIAL
                                         : envoy::data::ai::v3::TokenUsage::COMPLETE);
  }
  return typed;
}

} // namespace

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager& proto,
    Stats::Scope& scope)
    : stats_(AiProtocolManagerStats{
          ALL_AI_PROTOCOL_MANAGER_STATS(POOL_COUNTER_PREFIX(scope, "ai_protocol_manager."))}),
      request_handling_enabled_(proto.has_request_handling()),
      parse_unconfigured_routes_(proto.request_handling().parse_unconfigured_routes()),
      token_usage_enabled_(proto.response_handling().has_token_usage()),
      include_unconfigured_routes_(
          proto.response_handling().token_usage().include_unconfigured_routes()),
      default_api_protocol_(
          protocolFromProto(proto.response_handling().token_usage().default_api_protocol())),
      metadata_namespace_(proto.response_handling().token_usage().metadata_namespace().empty()
                              ? std::string(DefaultTokenUsageNamespace)
                              : proto.response_handling().token_usage().metadata_namespace()),
      max_sse_event_size_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto.response_handling().token_usage().limits(),
                                          max_sse_event_size, DefaultMaxSseEventSize)),
      max_json_body_size_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto.response_handling().token_usage().limits(),
                                          max_json_body_size, DefaultMaxJsonBodySize)),
      max_parsed_sse_events_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto.response_handling().token_usage().limits(),
                                          max_parsed_sse_events, DefaultMaxParsedSseEvents)) {}

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

Http::FilterHeadersStatus AiProtocolManagerFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                 bool end_stream) {
  // Request-side processing is off entirely; per-route declarations still
  // matter to the encode path, which resolves them itself.
  if (!config_->requestHandlingEnabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  // A headers-only request carries no payload to inspect, so there is nothing to
  // hold the chain for: let the headers flow. (Pausing here would also deadlock,
  // since no body would ever arrive to drive the replay that releases them.)
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Copy what the route declared; see the note on route_has_request_ for why the
  // config is not held by pointer.
  if (const RouteConfig* route_config =
          Http::Utility::resolveMostSpecificPerFilterConfig<RouteConfig>(decoder_callbacks_);
      route_config != nullptr) {
    route_has_request_ = route_config->hasRequest();
    route_request_protocol_ = route_config->requestProtocol();
    if (route_has_request_) {
      ENVOY_LOG(debug, "ai_protocol_manager: route declares request API {}",
                apiProtocolName(route_request_protocol_));
    }
  }

  // A declared AI endpoint is parsed strictly. Any other route is parsed only if
  // the filter opted into parsing unconfigured routes, and only for a request
  // that can be held to end of stream without stalling it.
  // TODO(penguingao): on a best-effort parse failure, release the held headers
  // and buffered body immediately and pass the remainder through unbuffered,
  // rather than buffering to end-of-stream.
  if (!isAiEndpoint() && (!config_->parseUnconfiguredRoutes() || !canHoldRequest(headers))) {
    ENVOY_LOG(trace, "ai_protocol_manager: route has no payload to inspect, passing through");
    return Http::FilterHeadersStatus::Continue;
  }

  request_parser_ = std::make_unique<JsonWithExtBufParser>(JsonWithExtBufParser::Config{});
  // Built here, not at setDecoderFilterCallbacks(), so a pass-through stream pays
  // for none of it: constructing it subscribes to upstream watermarks and claims
  // a schedulable callback.
  decode_manager_ = std::make_unique<BufferManager>(
      buffer_factory_, std::make_unique<DecoderFilterChainBridge>(*decoder_callbacks_));

  // Pin the headers so routing and admission filters do not act on them before
  // the payload is offloaded. decodeData() still fires while iteration is stopped
  // here; the headers are released when replay injects the first body frame (or,
  // for an empty/trailer-only body, when the manager continues iteration).
  ENVOY_LOG(trace, "ai_protocol_manager: holding headers until payload is offloaded");
  return Http::FilterHeadersStatus::StopIteration;
}

bool AiProtocolManagerFilter::feedParser(const Buffer::Instance& data, bool end_stream) {
  // Feed the frame's slices where they lie: the parser accepts a split at any
  // byte boundary, so there is no need to join them into one block. Copying here
  // would reintroduce the per-stream footprint the external buffer avoids.
  absl::Status status = absl::OkStatus();
  const Buffer::RawSliceVector slices = data.getRawSlices();
  for (size_t i = 0; i < slices.size() && status.ok(); ++i) {
    // Only the final slice of a terminal frame closes the document.
    const bool last_slice = (i + 1 == slices.size());
    status = request_parser_->feed(
        absl::string_view(static_cast<const char*>(slices[i].mem_), slices[i].len_),
        last_slice && end_stream);
  }
  if (status.ok() && end_stream && slices.empty()) {
    // getRawSlices() omits empty slices, so a terminal frame carrying no bytes
    // leaves the document open; close it here.
    status = request_parser_->feed("", /*end_stream=*/true);
  }

  if (!status.ok()) {
    if (isAiEndpoint()) {
      rejectInvalidPayload(status);
      return false;
    }
    // Best effort: the payload is forwarded as it stands, just without a
    // document for later filters to work from.
    ENVOY_LOG(debug, "ai_protocol_manager: forwarding unparsed payload: {}", status.message());
    request_parser_.reset();
    return true;
  }

  if (end_stream) {
    request_json_ = request_parser_->takeDocument();
    request_parser_.reset();

    if (isAiEndpoint()) {
      // TODO(penguingao): Support validating payload schema on the fly as the Wuffs parser
      // streams and parses chunks, rejecting invalid fields early before end_stream.
      if (const PayloadSchema* payload_schema =
              AdapterRegistry::get(route_request_protocol_).schema();
          payload_schema != nullptr) {
        const absl::Status validation_status = payload_schema->validateRequest(request_json_);
        if (!validation_status.ok()) {
          rejectInvalidPayload(validation_status);
          return false;
        }
      }
    }
  }
  return true;
}

void AiProtocolManagerFilter::rejectInvalidPayload(const absl::Status& status) {
  ENVOY_LOG(debug, "ai_protocol_manager: rejecting request: {}", status.message());
  payload_rejected_ = true;
  request_parser_.reset();
  decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, status.message(), nullptr,
                                     std::nullopt, "ai_protocol_manager_invalid_json");
}

Http::FilterDataStatus AiProtocolManagerFilter::decodeData(Buffer::Instance& data,
                                                           bool end_stream) {
  if (decode_manager_ == nullptr) {
    // decodeHeaders() decided this stream is none of our business.
    return Http::FilterDataStatus::Continue;
  }
  if (payload_rejected_) {
    // Already terminated by the local reply; drop whatever is still in flight.
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (request_parser_ != nullptr) {
    // The framing promised a body but no byte of one arrived -- a chunked request
    // whose only chunk is the terminator, or an empty terminal DATA frame. There
    // is no payload to validate, so the request passes through, the same as one
    // that ended on its headers. The parser has been fed nothing exactly when the
    // manager has been given nothing, since every frame reaches both.
    if (end_stream && data.length() == 0 && decode_manager_->empty()) {
      request_parser_.reset();
    } else if (!feedParser(data, end_stream)) {
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
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
  if (payload_rejected_) {
    return Http::FilterTrailersStatus::StopIteration;
  }
  // A trailer-only request (no body) has nothing to replay; let the trailers flow.
  if (decode_manager_->empty()) {
    return Http::FilterTrailersStatus::Continue;
  }

  // No data frame carried end_stream, so the document is still open. Closing it
  // here is what catches a truncated payload.
  if (request_parser_ != nullptr) {
    Buffer::OwnedImpl empty;
    if (!feedParser(empty, /*end_stream=*/true)) {
      return Http::FilterTrailersStatus::StopIteration;
    }
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
  if (config_ == nullptr || !config_->tokenUsageEnabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Route scoping: only declared AI endpoints are inspected unless the filter
  // opted into unconfigured routes, so enabling token usage on a mixed
  // listener does not silently parse unrelated JSON/SSE responses. Resolved
  // fresh here rather than cached from the decode path: the decode-path copy
  // is not taken when request handling is disabled, and it could be stale --
  // a later decode filter may refresh the route (clearRouteCache), and by
  // response time the route is frozen as the one that actually routed the
  // request.
  const RouteConfig* route_config =
      Http::Utility::resolveMostSpecificPerFilterConfig<RouteConfig>(encoder_callbacks_);
  if (route_config == nullptr && !config_->includeUnconfiguredRoutes()) {
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

  // The wire API to extract against, in precedence order: the route's declared
  // response API, the route's declared request API, the configured fallback;
  // Unspecified auto-detects from the response shape.
  ApiProtocol protocol = config_->defaultApiProtocol();
  if (route_config != nullptr &&
      route_config->effectiveResponseProtocol() != ApiProtocol::Unspecified) {
    protocol = route_config->effectiveResponseProtocol();
  }

  // Observation buffers charge the stream's memory account when tracking is
  // enabled; in an upstream installation the decoder callbacks resolve to the
  // downstream stream's account.
  const Buffer::BufferMemoryAccountSharedPtr account = decoder_callbacks_->account();
  if (is_sse) {
    response_handler_ = std::make_unique<SseResponseHandler>(protocol, config_->maxSseEventSize(),
                                                             config_->maxParsedSseEvents(),
                                                             config_->stats(), account);
  } else {
    auto handler = std::make_unique<JsonResponseHandler>(protocol, config_->maxJsonBodySize(),
                                                         config_->stats(), account);
    // A body already known (from content-length) to exceed the cap abandons
    // extraction up front, through the same transition the incremental cap
    // takes -- identical bodies produce the identical outcome regardless of
    // whether the length was advertised. onData() stays authoritative for
    // absent or wrong lengths.
    uint64_t content_length = 0;
    if (absl::SimpleAtoi(headers.getContentLengthValue(), &content_length) &&
        content_length > config_->maxJsonBodySize()) {
      handler->abandonOverLimit();
    }
    response_handler_ = std::move(handler);
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
  finalizeUsage(usage);
  const bool degraded = response_handler_->degraded() || usage.canonicalizationOverflow();
  if (!usage.hasAny() && !degraded) {
    // Legitimately absent usage: e.g. an OpenAI stream without
    // `stream_options.include_usage`, or an unrecognized response shape.
    config_->stats().token_usage_missing_.inc();
    return;
  }

  // Two publications for one stream (both-placement installs) would leave
  // consumers with an ambiguous record. First writer wins: the upstream
  // instance publishes first, later instances skip.
  const auto& existing_metadata =
      encoder_callbacks_->streamInfo().dynamicMetadata().typed_filter_metadata();
  if (existing_metadata.contains(config_->metadataNamespace())) {
    ENVOY_LOG(debug,
              "ai_protocol_manager: namespace {} already published for this stream "
              "(both-placement installation); skipping duplicate publication",
              config_->metadataNamespace());
    config_->stats().token_usage_duplicate_.inc();
    return;
  }

  // Convert the finalized accumulator once into the authoritative typed
  // record (envoy.data.ai.v3.TokenUsage). When extraction failed outright the
  // record is status-only (api_protocol/model/extraction_status, no counts),
  // letting consumers distinguish "failed to extract" from "no usage
  // supplied". Only typed metadata is published: consumers with full-fidelity
  // needs (ext_proc typed forwarding, filters reading typed metadata) share
  // the proto definition; no untyped Struct mirror is emitted.
  // TODO(botengyao): move this dynamic-metadata publication into a built-in
  // AI payload filter once the payload filter manager (the in-filter chain
  // over the parsed payload) lands. The filter core then only orchestrates
  // transport, and publication becomes an ordered, configurable chain entry
  // like any other payload feature.
  const envoy::data::ai::v3::TokenUsage typed = typedUsage(usage, degraded);
  // In both roles streamInfo() resolves to the downstream request's
  // StreamInfo. Under retries/hedging only the router-selected attempt
  // streams to a clean end of stream, so only the winner reaches this write.
  Protobuf::Any typed_any;
  MessageUtil::packFrom(typed_any, typed);
  encoder_callbacks_->streamInfo().setDynamicTypedMetadata(config_->metadataNamespace(), typed_any);

  if (!usage.hasAny()) {
    config_->stats().token_usage_failed_.inc();
    ENVOY_LOG(trace, "ai_protocol_manager: status-only (failed) record published to namespace {}",
              config_->metadataNamespace());
    return;
  }
  if (degraded) {
    config_->stats().token_usage_partial_.inc();
  }
  if (usage.provider_total_tokens.has_value() && usage.total_tokens.has_value() &&
      usage.provider_total_tokens.value() != usage.total_tokens.value()) {
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
