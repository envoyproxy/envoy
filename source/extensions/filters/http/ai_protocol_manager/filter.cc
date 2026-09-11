#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include <memory>

#include "envoy/common/exception.h"
#include "envoy/data/ai/v3/token_usage.pb.h"
#include "envoy/http/codes.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ai_protocol_manager/api_protocol_adapter.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_manager.h"
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
// Fits OpenAI Responses API terminal events, which embed the full response object.
constexpr uint32_t DefaultMaxSseEventSize = 1024 * 1024;
constexpr uint32_t DefaultMaxJsonBodySize = 4 * 1024 * 1024;
// Bounds the parse work a long-lived event stream can extract from the worker.
constexpr uint32_t DefaultMaxParsedSseEvents = 65536;

bool contentTypeMatches(absl::string_view content_type, absl::string_view expected) {
  const absl::string_view normalized = StringUtil::trim(StringUtil::cropRight(content_type, ";"));
  return absl::EqualsIgnoreCase(normalized, expected);
}

bool isJsonContentType(absl::string_view content_type) {
  const absl::string_view normalized = StringUtil::trim(StringUtil::cropRight(content_type, ";"));
  return absl::EqualsIgnoreCase(normalized, "application/json") ||
         absl::EndsWithIgnoreCase(normalized, "+json");
}

// Full-duplex streams may await a response before ending the request, so holding stalls them.
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
    Stats::Scope& scope, AiFilterFactories&& ai_filter_factories)
    : stats_(AiProtocolManagerStats{
          ALL_AI_PROTOCOL_MANAGER_STATS(POOL_COUNTER_PREFIX(scope, "ai_protocol_manager."))}),
      request_handling_enabled_(proto.has_request_handling()),
      parse_unconfigured_routes_(proto.request_handling().parse_unconfigured_routes()),
      inline_string_threshold_bytes_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          proto.request_handling().limits(), inline_string_threshold_bytes,
          JsonWithExtBufParser::kDefaultInlineStringThresholdBytes)),
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
                                          max_parsed_sse_events, DefaultMaxParsedSseEvents)),
      ai_filter_factories_(std::move(ai_filter_factories)) {}

absl::StatusOr<FilterConfigSharedPtr> FilterConfig::create(
    const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager& proto,
    Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope) {
  AiFilterFactories factories;
  for (const auto& entry : proto.request_handling().filters()) {
    auto* factory =
        Config::Utility::getAndCheckFactory<AiFilterConfigFactory>(entry, /*is_optional=*/true);
    if (factory == nullptr) {
      return absl::InvalidArgumentError(
          fmt::format("ai_protocol_manager: unknown AI filter '{}' with type URL '{}'",
                      entry.name(), Config::Utility::getFactoryType(entry.typed_config())));
    }
    const ProtobufTypes::MessagePtr config = factory->createEmptyConfigProto();
    RETURN_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
        entry.typed_config(), context.messageValidationVisitor(), *config));
    absl::StatusOr<AiFilterFactoryCb> cb = factory->createAiFilterFactory(*config, context, scope);
    RETURN_IF_NOT_OK_REF(cb.status());
    factories.push_back(std::move(cb.value()));
  }
  return std::make_shared<const FilterConfig>(proto, scope, std::move(factories));
}

void AiProtocolManagerFilter::onDestroy() {
  if (filter_manager_ != nullptr) {
    filter_manager_->cancel();
  }
  if (decode_manager_ != nullptr) {
    // Detach but do not free: this can run mid-replay, on the manager's own stack, when a
    // downstream local reply answers an injected frame. The filter's destruction frees it.
    decode_manager_->onDestroy();
  }
}

Http::FilterHeadersStatus AiProtocolManagerFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                 bool end_stream) {
  request_headers_ = &headers;
  if (!config_->requestHandlingEnabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Stopping here would deadlock: no body would arrive to drive the replay that releases it.
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }

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

  // TODO(penguingao): on a best-effort parse failure, stop buffering and pass the rest through.
  if (!isAiEndpoint() && (!config_->parseUnconfiguredRoutes() || !canHoldRequest(headers))) {
    ENVOY_LOG(trace, "ai_protocol_manager: route has no payload to inspect, passing through");
    return Http::FilterHeadersStatus::Continue;
  }

  JsonWithExtBufParser::Config parser_config;
  parser_config.inline_string_threshold_bytes = inlineStringThresholdBytes();
  request_parser_ = std::make_unique<JsonWithExtBufParser>(parser_config);
  // Built lazily: construction subscribes to watermarks and claims a schedulable callback.
  decode_manager_ = std::make_unique<BufferManager>(
      buffer_factory_,
      std::make_unique<DecoderFilterChainBridge>(*decoder_callbacks_, config_->stats()));

  // Held so later filters cannot act before the payload is offloaded; decodeData() still fires.
  // Released when replay injects the first body frame, or continues for an empty body.
  ENVOY_LOG(trace, "ai_protocol_manager: holding headers until payload is offloaded");
  return Http::FilterHeadersStatus::StopIteration;
}

uint32_t AiProtocolManagerFilter::inlineStringThresholdBytes() const {
  // A payload schema may pin its own: what must stay inline to validate is a wire-API property.
  if (isAiEndpoint()) {
    if (const PayloadSchema* payload_schema =
            AdapterRegistry::get(route_request_protocol_).schema();
        payload_schema != nullptr) {
      if (const std::optional<uint32_t> pinned =
              payload_schema->requestInlineStringThresholdBytes();
          pinned.has_value()) {
        return *pinned;
      }
    }
  }
  return config_->inlineStringThresholdBytes();
}

bool AiProtocolManagerFilter::feedParser(const Buffer::Instance& data, bool end_stream) {
  // The parser accepts a split at any byte, so slices are fed in place rather than copied.
  absl::Status status = absl::OkStatus();
  const Buffer::RawSliceVector slices = data.getRawSlices();
  for (size_t i = 0; i < slices.size() && status.ok(); ++i) {
    const bool last_slice = (i + 1 == slices.size());
    status = request_parser_->feed(
        absl::string_view(static_cast<const char*>(slices[i].mem_), slices[i].len_),
        last_slice && end_stream);
  }
  if (status.ok() && end_stream && slices.empty()) {
    // getRawSlices() omits empty slices, so an empty terminal frame closed nothing above.
    status = request_parser_->feed("", /*end_stream=*/true);
  }

  if (!status.ok()) {
    if (isAiEndpoint()) {
      config_->stats().request_parse_error_.inc();
      rejectInvalidPayload(status);
      return false;
    }
    config_->stats().request_passthrough_.inc();
    ENVOY_LOG(debug, "ai_protocol_manager: forwarding unparsed payload: {}", status.message());
    request_parser_.reset();
    return true;
  }

  if (end_stream) {
    request_json_ = request_parser_->takeDocument();
    request_parser_.reset();

    if (isAiEndpoint()) {
      // TODO(penguingao): validate the schema while streaming to reject invalid fields early.
      if (const PayloadSchema* payload_schema =
              AdapterRegistry::get(route_request_protocol_).schema();
          payload_schema != nullptr) {
        const absl::Status validation_status = payload_schema->validateRequest(request_json_);
        if (!validation_status.ok()) {
          config_->stats().request_schema_invalid_.inc();
          rejectInvalidPayload(validation_status);
          return false;
        }
      }
    }
    config_->stats().request_parsed_.inc();
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
    return Http::FilterDataStatus::Continue;
  }
  if (payload_rejected_) {
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (request_parser_ != nullptr) {
    // A zero-byte body passes through unvalidated, like a headers-only request. Every frame
    // reaches both parser and manager, so an empty manager means an unfed parser.
    if (end_stream && data.length() == 0 && decode_manager_->empty()) {
      request_parser_.reset();
    } else if (!feedParser(data, end_stream)) {
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
  }

  decode_manager_->onData(data);
  if (end_stream) {
    finalizeDecode(/*has_trailers=*/false);
  }
  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus AiProtocolManagerFilter::decodeTrailers(Http::RequestTrailerMap&) {
  if (decode_manager_ == nullptr) {
    return Http::FilterTrailersStatus::Continue;
  }
  if (payload_rejected_) {
    return Http::FilterTrailersStatus::StopIteration;
  }
  if (decode_manager_->empty()) {
    return Http::FilterTrailersStatus::Continue;
  }

  // No data frame carried end_stream; closing the document here catches a truncated payload.
  if (request_parser_ != nullptr) {
    Buffer::OwnedImpl empty;
    if (!feedParser(empty, /*end_stream=*/true)) {
      return Http::FilterTrailersStatus::StopIteration;
    }
  }

  finalizeDecode(/*has_trailers=*/true);
  // Held behind the replayed body until finalizeDecode()'s completion callback releases them.
  return Http::FilterTrailersStatus::StopIteration;
}

void AiProtocolManagerFilter::finalizeDecode(bool has_trailers) {
  decode_manager_->endStream();
  auto on_complete = [this, has_trailers](absl::Status status) {
    if (!status.ok()) {
      ENVOY_LOG(error, "ai_protocol_manager: replay failed: {}", status.message());
      if (!payload_rejected_) {
        payload_rejected_ = true;
        decoder_callbacks_->sendLocalReply(Http::Code::BadGateway, status.message(), nullptr,
                                           std::nullopt, "ai_protocol_manager_replay_error");
      }
      return;
    }
    if (has_trailers) {
      // Only after replay, so the held trailers follow the body.
      decoder_callbacks_->continueDecoding();
    } else {
      // Also releases the held headers when the body was empty.
      Buffer::OwnedImpl end_marker;
      decoder_callbacks_->injectDecodedDataToFilterChain(end_marker, /*end_stream=*/true);
    }
  };

  if (isAiEndpoint() && !decode_manager_->empty() && !payload_rejected_) {
    ASSERT(request_headers_ != nullptr);
    const AiFilterContext context{decoder_callbacks_->streamInfo(), *request_headers_,
                                  route_request_protocol_};
    std::vector<AiFilterPtr> filters;
    filters.reserve(config_->aiFilterFactories().size());
    for (const AiFilterFactoryCb& factory : config_->aiFilterFactories()) {
      if (AiFilterPtr filter = factory(context); filter != nullptr) {
        filters.push_back(std::move(filter));
      }
    }
    // TODO(penguingao): pass the upstream StreamInfo when installed in an upstream chain.
    filter_manager_ = std::make_unique<FilterManager>(
        std::move(filters), std::move(request_json_), decode_manager_.get(),
        decoder_callbacks_->dispatcher(), decoder_callbacks_->streamInfo(), request_headers_,
        [this](Http::Code code, std::string details) {
          ENVOY_LOG(debug, "ai_protocol_manager: rejecting request via local reply: {} {}",
                    static_cast<uint32_t>(code), details);
          payload_rejected_ = true;
          decoder_callbacks_->sendLocalReply(code, details, nullptr, std::nullopt,
                                             "ai_protocol_manager_filter_rejected");
        });
    filter_manager_->start([on_complete = std::move(on_complete)](absl::Status status) {
      on_complete(std::move(status));
    });
  } else {
    decode_manager_->replay(0, decode_manager_->length(), std::move(on_complete));
  }
}

Http::FilterHeadersStatus AiProtocolManagerFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                                 bool end_stream) {
  if (config_ == nullptr || !config_->tokenUsageEnabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Resolved fresh: the decode-path copy is skipped when request handling is off and goes
  // stale if a later decode filter clears the route cache.
  const RouteConfig* route_config =
      Http::Utility::resolveMostSpecificPerFilterConfig<RouteConfig>(encoder_callbacks_);
  if (route_config == nullptr && !config_->includeUnconfiguredRoutes()) {
    return Http::FilterHeadersStatus::Continue;
  }

  const auto status = Http::Utility::getResponseStatusOrNullopt(headers);
  if (!status.has_value() || status.value() < 200 || status.value() >= 300) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Before the encoding check, so only otherwise-inspected responses count an encoding skip.
  const absl::string_view content_type = headers.getContentTypeValue();
  const bool is_sse = contentTypeMatches(content_type, SseContentType);
  const bool is_json = !is_sse && contentTypeMatches(content_type, JsonContentType);
  if (!is_sse && !is_json) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (!contentEncodingIsIdentity(headers)) {
    config_->stats().unsupported_content_encoding_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  // Counted like an empty terminal body, so the outcome is independent of codec framing.
  if (end_stream) {
    config_->stats().token_usage_missing_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  // Unspecified auto-detects from the response shape.
  ApiProtocol protocol = config_->defaultApiProtocol();
  if (route_config != nullptr &&
      route_config->effectiveResponseProtocol() != ApiProtocol::Unspecified) {
    protocol = route_config->effectiveResponseProtocol();
  }

  // In an upstream installation this resolves to the downstream stream's account.
  const Buffer::BufferMemoryAccountSharedPtr account = decoder_callbacks_->account();
  if (is_sse) {
    response_handler_ = std::make_unique<SseResponseHandler>(protocol, config_->maxSseEventSize(),
                                                             config_->maxParsedSseEvents(),
                                                             config_->stats(), account);
  } else {
    auto handler = std::make_unique<JsonResponseHandler>(protocol, config_->maxJsonBodySize(),
                                                         config_->stats(), account);
    // Same transition as the incremental cap; onData() still covers absent or wrong lengths.
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
    // Trailers end the body, so the handler must see end of stream before finalizing.
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
    // e.g. an OpenAI stream without `stream_options.include_usage`.
    config_->stats().token_usage_missing_.inc();
    return;
  }

  // First writer wins across both-placement installs; the upstream instance publishes first.
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

  // A status-only (FAILED) record lets consumers tell failed extraction from absent usage.
  // TODO(botengyao): move this publication into a built-in AI payload filter.
  const envoy::data::ai::v3::TokenUsage typed = typedUsage(usage, degraded);
  // Downstream StreamInfo in both roles; only the router-selected retry/hedge attempt gets here.
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
