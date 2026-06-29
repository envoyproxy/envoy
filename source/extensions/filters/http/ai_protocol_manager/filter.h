#pragma once

#include <cstdint>

#include "envoy/http/codec.h"

#include "source/common/common/logger.h"
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
// Flow control is enforced in both directions:
//   - Ingest: appends to the external buffer honor its in-flight watermark and
//     push back on the downstream source (the client) when the backing store
//     cannot keep up (see ExternalBufferWatermarkCallbacks below).
//   - Replay: re-injected data is paced against upstream back-pressure. When
//     the chain we feed (router -> upstream) backs up, the router's
//     UpstreamRequest raises back-pressure toward the request source; the
//     connection manager fans this out to UpstreamWatermarkCallbacks
//     subscribers, and we pause issuing reads/injects until it drains. Without
//     this the read->inject loop would push the entire payload into
//     buffered_request_data_ regardless of how fast the upstream drains it,
//     defeating the bounded-footprint goal (and risking a 413). Note the
//     connection manager's own response to that back-pressure -- read-disabling
//     the downstream codec -- does nothing for us, since our replayed data comes
//     from the external buffer, not the codec.
//
// Current behavior is a straight offload-then-replay. Streaming JSON parsing and
// admission control will be layered on top of this plumbing.
class AiProtocolManagerFilter : public Http::PassThroughFilter,
                                public ExternalBufferWatermarkCallbacks,
                                public Http::UpstreamWatermarkCallbacks,
                                public Logger::Loggable<Logger::Id::filter> {
public:
  explicit AiProtocolManagerFilter(ExternalBufferFactory& buffer_factory)
      : buffer_factory_(buffer_factory) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

  // ExternalBufferWatermarkCallbacks (ingest side: backing store back-pressure).
  void onAboveHighWatermark() override;
  void onBelowLowWatermark() override;

  // Http::UpstreamWatermarkCallbacks (replay side: upstream back-pressure).
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

private:
  // Completion handler for an append() issued from decodeData().
  void onAppendComplete(ExternalBufferStatus status);
  // Kicks off reading the offloaded payload back into the filter chain.
  void streamBackToFilterChain();
  // Issues the next bounded read() in the replay, unless replay is finished or
  // currently paused by upstream back-pressure (or a read is already in flight).
  void maybeReadNextChunk();
  // Completion handler for a read() issued during replay.
  void onReadComplete(ExternalBufferStatus status, Buffer::InstancePtr data);
  // Fails the request when an external-buffer operation errors out.
  void onExternalBufferError();

  // Size of each chunk streamed back to the filter chain during replay. Keeps
  // the replay footprint bounded regardless of total payload size.
  static constexpr uint64_t ReadChunkSize = 64 * 1024;

  ExternalBufferFactory& buffer_factory_;
  ExternalBufferPtr buffer_;

  // True once decodeData() has observed end_stream. Replay begins once this is
  // set and all outstanding appends have completed.
  bool end_stream_seen_{false};
  // Number of append() calls whose completion callback has not yet fired.
  uint64_t outstanding_appends_{0};

  // True between streamBackToFilterChain() and the final injected frame.
  bool replaying_{false};
  // True while a replay read() is outstanding; prevents overlapping reads.
  bool read_in_flight_{false};
  // Replay cursor: next offset to read and the total length being replayed.
  uint64_t replay_offset_{0};
  uint64_t replay_length_{0};

  // Depth of unmatched upstream high-watermark callbacks. The connection manager
  // may raise the watermark more than once (stream and connection), so we resume
  // replay only when this returns to zero. Non-zero => replay paused.
  uint32_t upstream_high_watermark_count_{0};
  // Whether *this is currently registered as an UpstreamWatermarkCallbacks.
  bool watermark_callbacks_registered_{false};

  // Set in onDestroy(); guards the async completion handlers against touching
  // filter callbacks after the stream is gone.
  bool destroyed_{false};
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
