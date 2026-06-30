#pragma once

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

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
// The offload/replay pipeline and its bidirectional flow control live in the
// path-agnostic BufferManager (buffer_manager.h); the filter is a thin delegator
// that constructs one BufferManager per direction with the matching
// FilterChainBridge (filter_chain_bridge.h). Today only the decode (request) path
// is wired; the encode path will construct a second BufferManager with the
// encoder bridge.
//
// The offload/replay plumbing is in place today; streaming JSON parsing and
// validation, and an AI-specific extension chain to store, manipulate, and
// rewrite the payload before replay, will be layered on in later changes.
class AiProtocolManagerFilter : public Http::PassThroughFilter,
                                public Logger::Loggable<Logger::Id::filter> {
public:
  explicit AiProtocolManagerFilter(ExternalBufferFactory& buffer_factory)
      : buffer_factory_(buffer_factory) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

private:
  ExternalBufferFactory& buffer_factory_;
  BufferManagerPtr decode_manager_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
