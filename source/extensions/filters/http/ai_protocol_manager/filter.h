#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/router/router.h"
#include "envoy/stats/scope.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf_parser.h"
#include "source/extensions/filters/http/ai_protocol_manager/response_handler.h"
#include "source/extensions/filters/http/ai_protocol_manager/stats.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

using PerRouteProto =
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute;

// The inverse pair mapping the shared wire-API enum (envoy.type.ai.v3
// .ApiProtocol) to and from the internal mirror. Kept side by side as two
// exhaustive switches -- a shared runtime table would trade away the
// compiler's missing-case checking when either enum grows.

// Unrecognized values -- possible only across a version skew, since configs
// are validated defined_only -- auto-detect.
inline ApiProtocol protocolFromProto(envoy::type::ai::v3::ApiProtocol protocol) {
  switch (protocol) {
  case envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS:
    return ApiProtocol::OpenAiChatCompletions;
  case envoy::type::ai::v3::OPENAI_RESPONSES:
    return ApiProtocol::OpenAiResponses;
  case envoy::type::ai::v3::ANTHROPIC_MESSAGES:
    return ApiProtocol::AnthropicMessages;
  case envoy::type::ai::v3::GEMINI_GENERATE_CONTENT:
    return ApiProtocol::GeminiGenerateContent;
  default:
    return ApiProtocol::Unspecified;
  }
}

inline envoy::type::ai::v3::ApiProtocol protocolToProto(ApiProtocol protocol) {
  switch (protocol) {
  case ApiProtocol::OpenAiChatCompletions:
    return envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS;
  case ApiProtocol::OpenAiResponses:
    return envoy::type::ai::v3::OPENAI_RESPONSES;
  case ApiProtocol::AnthropicMessages:
    return envoy::type::ai::v3::ANTHROPIC_MESSAGES;
  case ApiProtocol::GeminiGenerateContent:
    return envoy::type::ai::v3::GEMINI_GENERATE_CONTENT;
  case ApiProtocol::Unspecified:
    break;
  }
  return envoy::type::ai::v3::API_PROTOCOL_UNSPECIFIED;
}

// Filter-level configuration, shared by every stream on the chain: which
// directions are enabled, the token-usage extraction settings, and the
// filter's stats. Shared across downstream and upstream installations alike.
class FilterConfig {
public:
  FilterConfig(
      const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager& proto,
      Stats::Scope& scope);

  bool requestHandlingEnabled() const { return request_handling_enabled_; }
  bool parseUnconfiguredRoutes() const { return parse_unconfigured_routes_; }
  bool tokenUsageEnabled() const { return token_usage_enabled_; }
  bool includeUnconfiguredRoutes() const { return include_unconfigured_routes_; }
  ApiProtocol defaultApiProtocol() const { return default_api_protocol_; }
  const std::string& metadataNamespace() const { return metadata_namespace_; }
  uint32_t maxSseEventSize() const { return max_sse_event_size_; }
  uint32_t maxJsonBodySize() const { return max_json_body_size_; }
  uint32_t maxParsedSseEvents() const { return max_parsed_sse_events_; }
  AiProtocolManagerStats& stats() const { return stats_; }

private:
  // Counters are thread-safe to increment; mutable so a shared const config
  // serves them.
  mutable AiProtocolManagerStats stats_;
  const bool request_handling_enabled_ = false;
  const bool parse_unconfigured_routes_ = false;
  const bool token_usage_enabled_ = false;
  const bool include_unconfigured_routes_ = false;
  const ApiProtocol default_api_protocol_ = ApiProtocol::Unspecified;
  const std::string metadata_namespace_;
  const uint32_t max_sse_event_size_ = 0;
  const uint32_t max_json_body_size_ = 0;
  const uint32_t max_parsed_sse_events_ = 0;
};
using FilterConfigSharedPtr = std::shared_ptr<const FilterConfig>;

// Per-route configuration. Its presence declares the route an AI endpoint.
// The request and response wire APIs are declared separately (protocol
// translation can make them differ); either may be Unspecified when the
// route left it undeclared. A declared request API with a registered payload
// schema (schema/schema_registry.h) is validated strictly.
class RouteConfig : public Router::RouteSpecificFilterConfig {
public:
  explicit RouteConfig(const PerRouteProto& proto)
      : has_request_(proto.has_request()),
        request_protocol_(protocolFromProto(proto.request().api_protocol())),
        response_protocol_(protocolFromProto(proto.response().api_protocol())) {}

  // Whether the route hands its request payload to the filter to hold and
  // validate.
  bool hasRequest() const { return has_request_; }
  ApiProtocol requestProtocol() const { return request_protocol_; }
  ApiProtocol responseProtocol() const { return response_protocol_; }

  // The wire API for response extraction on this route: the declared response
  // API, falling back to the declared request API.
  ApiProtocol effectiveResponseProtocol() const {
    return response_protocol_ != ApiProtocol::Unspecified ? response_protocol_ : request_protocol_;
  }

private:
  const bool has_request_ = false;
  const ApiProtocol request_protocol_ = ApiProtocol::Unspecified;
  const ApiProtocol response_protocol_ = ApiProtocol::Unspecified;
};

// AI Protocol Manager HTTP filter (alpha).
//
// The filter manages AI endpoint traffic: it holds a request payload and
// parses it against the wire API the endpoint declares -- which is what lets
// routing, admission and policy act on a payload the proxy understands rather
// than on opaque bytes.
//
// As the body arrives the filter offloads it into an ExternalBuffer -- keeping
// a large payload out of the connection manager's buffers -- and parses and
// validates the JSON in a streaming fashion alongside. The chain is held
// meanwhile: decodeHeaders() stops iteration when a body follows, and the
// headers stay pinned here while decodeData() keeps offloading. Only once the
// payload is validated does the filter replay the buffered body back into the
// chain; the first injectDecodedDataToFilterChain() call releases the held
// headers ahead of it, so subsequent filters see the headers immediately
// followed by the payload. An invalid payload is rejected rather than
// forwarded.
//
// None of that happens for a stream the filter has no reason to inspect:
// decodeHeaders() returns Continue and the offload path is never entered.
//
// The offload/replay pipeline and its bidirectional flow control live in the
// path-agnostic BufferManager (buffer_manager.h); the filter is a thin delegator
// that constructs one BufferManager per direction with the matching
// FilterChainBridge (filter_chain_bridge.h). Today only the decode (request) path
// is wired; the encode path will construct a second BufferManager with the
// encoder bridge.
//
// Parsing runs alongside the offload: every body frame is fed to a
// JsonWithExtBufParser before it reaches the BufferManager, so the two see the
// identical byte stream from the first body byte -- which is what makes the
// parser's recorded offsets valid buffer offsets (json_with_ext_buf_parser.h).
// Feeding first also fails a malformed payload the moment the bad byte arrives,
// not after the whole upload.
//
// A route carrying a per-route request declaration is a declared AI endpoint,
// and its payload is the filter's to manage: parsed strictly, with a malformed
// one rejected so Envoy and the backend cannot read the same body differently.
// A route without one is parsed only if the filter opted into
// parse_unconfigured_routes -- offered for compatibility with chains that want
// a parsed body on ordinary routes, never a reason to fail a request -- and is
// otherwise untouched. That opt-in covers routes that never asked for it, so it
// takes only a request it can hold to end of stream without stalling it, which
// rules out gRPC and Connect streaming, upgrades, and CONNECT. A declared
// endpoint carries no such gate.
//
// A declared wire API with a registered payload schema is validated at end of
// payload (schema/schema_registry.h); normalization comes later.
//
// Encode (response) path: observe-only token-usage extraction. When
// response_handling.token_usage is configured, 2xx SSE/JSON responses on
// configured routes (or on all routes with include_unconfigured_routes) are
// teed into a ResponseHandler; at end of stream the normalized usage is
// published as dynamic metadata on the downstream StreamInfo, from downstream
// and upstream installations alike. The filter never stops iteration or
// mutates the response; extraction works on bounded side state, synchronously
// on the encode callbacks, and no handler failure can affect the stream.
class AiProtocolManagerFilter : public Http::PassThroughFilter,
                                public Logger::Loggable<Logger::Id::filter> {
public:
  AiProtocolManagerFilter(ExternalBufferFactory& buffer_factory, FilterConfigSharedPtr config)
      : buffer_factory_(buffer_factory), config_(std::move(config)) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

private:
  // Feeds one body frame to the parser in place. Returns false only if the
  // payload was rejected, in which case the caller must not offload or replay
  // it; a best-effort parse that fails abandons parsing and returns true.
  bool feedParser(const Buffer::Instance& data, bool end_stream);

  // Terminates the stream with a 400 for a payload that failed to parse.
  void rejectInvalidPayload(const absl::Status& status);

  // Whether the route handed its request payload to the filter, which is also
  // what makes a parse failure fatal.
  bool isAiEndpoint() const { return route_has_request_; }

  // Publish the accumulated token usage as dynamic metadata and account stats.
  // Called exactly once, at response end of stream (data or trailers).
  void finalizeResponseHandling();

  ExternalBufferFactory& buffer_factory_;
  FilterConfigSharedPtr config_;

  // Non-null exactly when decodeHeaders() decided to inspect this stream, so it
  // doubles as the engaged flag. Outlives request_parser_, which is released as
  // soon as parsing is done with.
  BufferManagerPtr decode_manager_;

  // Copied out of the route configuration rather than held by pointer: the route
  // can be re-resolved mid-stream, which would leave a cached pointer dangling,
  // and these are two scalars.
  bool route_has_request_{false};
  ApiProtocol route_request_protocol_{ApiProtocol::Unspecified};

  // The parsed payload. Populated once the body has been fully received and
  // parsed; nothing consumes it yet.
  JsonWithExtBuf request_json_;
  // Cleared once parsing is done with, whether it completed, was abandoned, or
  // failed the request.
  std::unique_ptr<JsonWithExtBufParser> request_parser_;

  // Once set, later frames on the dying stream are dropped, not offloaded.
  bool payload_rejected_{false};

  // Encode-path (response token-usage) state.
  ResponseHandlerPtr response_handler_;
  bool response_finalized_{false};
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
