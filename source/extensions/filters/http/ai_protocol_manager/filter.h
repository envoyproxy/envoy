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
// Routing and admission decisions for AI traffic can only be made once the
// full request payload has been parsed and validated, but the payload can be
// large and we must not pin it all in the connection manager's buffers. This
// filter therefore offloads the request body into an ExternalBuffer as it
// arrives, and once the stream ends it streams the bytes back into the filter
// chain for the downstream filters (and, eventually, the parser/validator) to
// consume.
//
// The offload/replay pipeline and its bidirectional flow control live in the
// path-agnostic BufferManager (buffer_manager.h); the filter is a thin
// delegator that constructs one BufferManager per direction with the matching
// FilterChainBridge (filter_chain_bridge.h). Today only the decode (request)
// path is wired; the encode path will construct a second BufferManager with the
// encoder bridge.
//
// Current behavior is a straight offload-then-replay. Streaming JSON parsing and
// admission control will be layered on top of this plumbing.
class AiProtocolManagerFilter : public Http::PassThroughFilter,
                                public Logger::Loggable<Logger::Id::filter> {
public:
  explicit AiProtocolManagerFilter(ExternalBufferFactory& buffer_factory)
      : buffer_factory_(buffer_factory) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

private:
  ExternalBufferFactory& buffer_factory_;
  BufferManagerPtr decode_manager_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
