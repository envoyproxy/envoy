#pragma once

#include <cstdint>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"

#include "absl/functional/any_invocable.h"

namespace Envoy {
namespace ExternalBuffer {

// Invoked once a replay() range has been fully injected into the filter chain. The
// caller can start to stream further sub-ranges or to terminate the stream.
using ReplayDoneCallback = absl::AnyInvocable<void()>;

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

  // Re-injects a replayed data frame into the filter chain. Always a non-terminal
  // frame: end-of-stream is the caller's concern (see ReplayDoneCallback).
  virtual void injectData(Buffer::Instance& data) PURE;

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

} // namespace ExternalBuffer
} // namespace Envoy
