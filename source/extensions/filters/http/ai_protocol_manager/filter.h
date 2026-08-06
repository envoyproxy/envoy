#pragma once

#include <memory>

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
// Parsing runs alongside the offload: every body frame is fed to a
// JsonWithExtBufParser before it reaches the BufferManager, so the two see the
// identical byte stream from the first body byte -- which is what makes the
// parser's recorded offsets valid buffer offsets (json_with_ext_buf_parser.h).
// Feeding first also fails a malformed payload the moment the bad byte arrives,
// not after the whole upload. Only a JSON content type is parsed; anything else
// is offloaded and replayed untouched.
//
// The document (requestJson()) is not yet consumed -- an AI-specific extension
// chain to inspect and rewrite the payload comes later. What it already buys is
// validation: malformed JSON, duplicate keys, and unrepresentable numbers are
// rejected here rather than forwarded for the backend to read differently.
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

  // The parsed payload, or nullptr when there was none to parse (no body, or a
  // non-JSON content type). Only complete once the full body has arrived. Its
  // ExternalRefs point into the BufferManager's buffer, so it is valid only for
  // this filter's lifetime.
  const JsonWithExtBuf* requestJson() const { return request_json_.get(); }

private:
  // Feeds one body frame to the parser in place. Returns false if the payload
  // was rejected, in which case the caller must not offload or replay it.
  bool feedParser(const Buffer::Instance& data, bool end_stream);

  // Terminates the stream with a 400 for a payload that failed to parse.
  void rejectInvalidPayload(const absl::Status& status);

  ExternalBufferFactory& buffer_factory_;
  BufferManagerPtr decode_manager_;

  // The parser is cleared once a payload is rejected; request_json_ outlives it.
  std::unique_ptr<JsonWithExtBuf> request_json_;
  std::unique_ptr<JsonWithExtBufParser> request_parser_;

  // Whether any body byte reached the parser. Distinguishes "no body at all",
  // passed through unvalidated like a headers-only request, from a body that
  // ended on an empty terminal frame, which still has to close the document.
  bool parser_fed_{false};

  // Once set, later frames on the dying stream are dropped, not offloaded.
  bool payload_rejected_{false};
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
