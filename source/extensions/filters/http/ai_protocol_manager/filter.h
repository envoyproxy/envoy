#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/router/router.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf_parser.h"
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

// Filter-level configuration, shared by every stream on the chain.
class FilterConfig {
public:
  explicit FilterConfig(
      const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager& proto)
      : best_effort_parsing_(proto.best_effort_parsing()) {}

  bool bestEffortParsing() const { return best_effort_parsing_; }

private:
  const bool best_effort_parsing_;
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
// AI requests carry a JSON payload the filter must vet before the rest of the
// chain acts on the request. As the body arrives the filter offloads it into an
// ExternalBuffer -- keeping a large payload out of the connection manager's
// buffers -- and parses and validates the JSON in a streaming fashion alongside.
// The chain is held meanwhile: decodeHeaders() stops iteration when a body
// follows, and the headers stay pinned here while decodeData() keeps offloading.
// Only once the payload is validated does the filter replay the buffered body
// back into the chain; the first injectDecodedDataToFilterChain() call releases
// the held headers ahead of it, so subsequent filters see the headers immediately
// followed by the payload. An invalid payload is rejected rather than forwarded.
//
// None of that happens unless the filter has a reason to look at the payload.
// decodeHeaders() decides once per stream; if not, it returns Continue and the
// stream never touches the offload path -- no external buffer, no watermark
// subscription, no held headers.
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
// Whether a payload is parsed at all, and whether a parse failure is fatal, is
// decided per route. A route carrying a RouteConfig is a declared AI endpoint:
// its payload is parsed strictly and a malformed one is rejected with a 400,
// which is what keeps Envoy and the backend from reading the same body
// differently. A route without one is parsed on a best-effort basis if the
// filter is configured for it -- the document is offered to later filters when
// it parses and the request is forwarded untouched when it does not -- and is
// otherwise not parsed.
//
// Neither the document nor the route's schema is consumed yet: validation
// against the schema, transcoding to the canonical schema when the route asks to
// normalize, and exposing the document to the rest of the chain all come later.
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
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
