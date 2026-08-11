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
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

using PerRouteProto =
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute;
// The schema a route declares its payload follows. UNSPECIFIED means the route
// declared nothing, i.e. it is not an AI endpoint.
using Schema = PerRouteProto::Schema;

// Filter-level configuration, shared by every stream on the chain: the
// request-side parsing mode, the parsed response-handling settings, and the
// filter's stats. Shared across downstream and upstream installations alike.
class FilterConfig {
public:
  FilterConfig(
      const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager& proto,
      Stats::Scope& scope);

  bool bestEffortParsing() const { return best_effort_parsing_; }
  bool responseHandlingEnabled() const { return response_handling_enabled_; }
  ApiFormat apiFormat() const { return api_format_; }
  const std::string& metadataNamespace() const { return metadata_namespace_; }
  uint32_t maxEventSize() const { return max_event_size_; }
  uint32_t maxInspectedBodySize() const { return max_inspected_body_size_; }
  uint32_t maxParsedEvents() const { return max_parsed_events_; }
  AiProtocolManagerStats& stats() const { return stats_; }

private:
  // Counters are thread-safe to increment; mutable so a shared const config
  // serves them.
  mutable AiProtocolManagerStats stats_;
  const bool best_effort_parsing_;
  const bool response_handling_enabled_;
  const ApiFormat api_format_;
  const std::string metadata_namespace_;
  const uint32_t max_event_size_;
  const uint32_t max_inspected_body_size_;
  const uint32_t max_parsed_events_;
};
using FilterConfigSharedPtr = std::shared_ptr<const FilterConfig>;

// Per-route configuration. Its presence declares the route an AI endpoint: the
// payload is parsed strictly and, once schemas land, validated against schema()
// and transcoded to the canonical schema when normalize() is set.
class RouteConfig : public Router::RouteSpecificFilterConfig {
public:
  explicit RouteConfig(const PerRouteProto& proto)
      : schema_(proto.schema()), normalize_(proto.normalize()) {}

  Schema schema() const { return schema_; }
  bool normalize() const { return normalize_; }

private:
  const Schema schema_;
  const bool normalize_;
};

// AI Protocol Manager HTTP filter (alpha).
//
// The filter manages AI endpoint traffic: it holds a request payload, validates
// it against the schema the endpoint serves, and normalizes it to a canonical
// schema -- which is what lets routing, admission and policy act on a payload
// the proxy understands rather than on opaque bytes.
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
// A route carrying a RouteConfig is a declared AI endpoint, and its payload is
// the filter's to manage: parsed strictly, with a malformed one rejected so
// Envoy and the backend cannot read the same body differently. A route without
// one is parsed only if the filter was configured for best effort -- offered for
// compatibility with chains that want a parsed body on ordinary routes, never a
// reason to fail a request -- and is otherwise untouched.
//
// The schema is not acted on yet: validation against it and transcoding to the
// canonical schema when the route asks to normalize both come later.
//
// Encode (response) path: observe-only token-usage extraction. When
// response_handling is configured, 2xx SSE/JSON responses are teed into a
// ResponseHandler; at end of stream the normalized usage is published as
// dynamic metadata on the downstream StreamInfo, from downstream and upstream
// installations alike. The filter never stops iteration or mutates the
// response; extraction works on bounded side state, synchronously on the
// encode callbacks, and no handler failure can affect the stream.
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

  // Whether the route declared itself an AI endpoint, which is also what makes a
  // parse failure fatal.
  bool isAiEndpoint() const { return schema_ != PerRouteProto::UNSPECIFIED; }

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
  Schema schema_{PerRouteProto::UNSPECIFIED};
  bool normalize_{false};

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
