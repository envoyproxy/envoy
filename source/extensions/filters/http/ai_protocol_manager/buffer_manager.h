#pragma once

#include <cstdint>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Replay-side flow-control sink. The BufferManager implements this so a
// FilterChainBridge can forward the path-specific Envoy watermark callback
// (UpstreamWatermarkCallbacks on the decode path, DownstreamWatermarkCallbacks
// on the encode path) into the manager without the manager having to inherit
// either of those identically-named-but-distinct interfaces.
class ReplayWatermarkHandler {
public:
  virtual ~ReplayWatermarkHandler() = default;

  // The chain we replay into is backed up; pause issuing reads/injects.
  virtual void onReplayAboveHighWatermark() PURE;
  // The chain has drained; replay may resume.
  virtual void onReplayBelowLowWatermark() PURE;
};

// Path-agnostic view of the Envoy filter chain. The BufferManager talks only to
// this interface, so the same offload/replay logic serves both the decode and
// encode paths; each path supplies a concrete adapter (see filter_chain_bridge.h)
// that maps these methods onto the corresponding decoder/encoder callbacks.
//
// A bridge is owned by the BufferManager it serves, so it always outlives the
// manager's async callbacks.
class FilterChainBridge {
public:
  virtual ~FilterChainBridge() = default;

  // Dispatcher the external buffer should use for completion/watermark callbacks.
  virtual Event::Dispatcher& dispatcher() PURE;
  // High watermark for in-flight ingest data (the configured decoder/encoder
  // buffer limit).
  virtual uint32_t bufferLimit() PURE;
  // Re-injects replayed data into the filter chain.
  virtual void injectData(Buffer::Instance& data, bool end_stream) PURE;
  // Pushes ingest back-pressure toward the data source (the filter's own
  // write-buffer high/low watermark on this path).
  virtual void pauseSource() PURE;
  virtual void resumeSource() PURE;
  // Subscribes/unsubscribes `handler` to the path's replay back-pressure
  // (upstream watermarks on decode, downstream watermarks on encode).
  virtual void registerReplayWatermarks(ReplayWatermarkHandler& handler) PURE;
  virtual void unregisterReplayWatermarks() PURE;
  // Fails the stream after an unrecoverable external-buffer error.
  virtual void onUnrecoverableError() PURE;
};
using FilterChainBridgePtr = std::unique_ptr<FilterChainBridge>;

// Owns the "offload the payload into an ExternalBuffer, then replay it back into
// the filter chain once the stream ends" pipeline, with flow control in both
// directions. All interaction with the filter chain goes through the owned
// FilterChainBridge, so the manager is path-agnostic: the decode and encode
// paths each construct a BufferManager with the appropriate bridge.
//
// Flow control is enforced in both directions:
//   - Ingest: appends honor the external buffer's in-flight watermark and push
//     back on the data source via the bridge when the backing store cannot keep
//     up (ExternalBufferWatermarkCallbacks below).
//   - Replay: re-injected data is paced against the chain's back-pressure,
//     delivered through ReplayWatermarkHandler. We pause issuing reads/injects
//     while the chain is backed up; without this the read->inject loop would
//     push the entire payload downstream regardless of drain rate, defeating the
//     bounded-footprint goal.
class BufferManager : public ExternalBufferWatermarkCallbacks,
                      public ReplayWatermarkHandler,
                      Logger::Loggable<Logger::Id::filter> {
public:
  BufferManager(ExternalBufferFactory& buffer_factory, FilterChainBridgePtr bridge);

  // Entry point for the path's data callback (decodeData/encodeData). Offloads
  // `data` into the external buffer and holds the chain; replay begins once the
  // stream has been fully offloaded.
  Http::FilterDataStatus onData(Buffer::Instance& data, bool end_stream);
  // Cancels in-flight work and detaches from the filter chain. After this the
  // async completion handlers are inert.
  void onDestroy();

  // ExternalBufferWatermarkCallbacks (ingest side: backing-store back-pressure).
  void onAboveHighWatermark() override;
  void onBelowLowWatermark() override;

  // ReplayWatermarkHandler (replay side: filter-chain back-pressure).
  void onReplayAboveHighWatermark() override;
  void onReplayBelowLowWatermark() override;

private:
  // Completion handler for an append() issued from onData().
  void onAppendComplete(ExternalBufferStatus status);
  // Kicks off reading the offloaded payload back into the filter chain.
  void streamBackToFilterChain();
  // Issues the next bounded read() in the replay, unless replay is finished or
  // currently paused by chain back-pressure (or a read is already in flight).
  void maybeReadNextChunk();
  // Completion handler for a read() issued during replay.
  void onReadComplete(ExternalBufferStatus status, Buffer::InstancePtr data);
  // Fails the stream when an external-buffer operation errors out.
  void onExternalBufferError();

  // Size of each chunk streamed back to the filter chain during replay. Keeps
  // the replay footprint bounded regardless of total payload size.
  static constexpr uint64_t ReadChunkSize = 64 * 1024;

  ExternalBufferFactory& buffer_factory_;
  FilterChainBridgePtr bridge_;
  ExternalBufferPtr buffer_;

  // True once onData() has observed end_stream. Replay begins once this is set
  // and all outstanding appends have completed.
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

  // Depth of unmatched replay high-watermark callbacks. The connection manager
  // may raise the watermark more than once (stream and connection), so we resume
  // replay only when this returns to zero. Non-zero => replay paused.
  uint32_t replay_high_watermark_count_{0};

  // Set in onDestroy(); guards the async completion handlers against touching
  // the bridge after the stream is gone.
  bool destroyed_{false};
};

using BufferManagerPtr = std::unique_ptr<BufferManager>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
