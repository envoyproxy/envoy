#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/filter.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/stats.h"

#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Path-agnostic view of the Envoy HTTP filter chain, hiding the minor API differences between the
// decode (request) and encode (response) paths and owning a path's flow control.
//
// Each path supplies a concrete adapter that maps the virtual methods onto the corresponding
// StreamDecoderFilterCallbacks / StreamEncoderFilterCallbacks -- including data injection, source
// watermark signaling, local-reply error reporting, and watermark subscription (upstream
// watermarks on decode; downstream watermarks subscribed via decoder callbacks on encode). The
// bridge is shared by every ingest client and ChainWriter on the path and outlives all of them:
// ingested data on a path comes from a single source, so ingest back-pressure is aggregated across
// registered clients.
class FilterChainBridge : public Logger::Loggable<Logger::Id::filter> {
public:
  using WriteResumeCallback = absl::AnyInvocable<void()>;

  // Lifecycle and flow-control hooks implemented by ingest managers registered with a bridge.
  class Client {
  public:
    virtual ~Client() = default;
    virtual void onBridgeDetached() PURE;
    virtual void onIngestPaused() PURE;
  };

  // `buffer_limit` is the chain's ingest high watermark (the configured decoder/encoder buffer
  // limit); 0 disables ingest flow control.
  explicit FilterChainBridge(uint32_t buffer_limit)
      : high_watermark_(buffer_limit), low_watermark_(buffer_limit / 2) {}

  virtual ~FilterChainBridge();

  // Registers/unregisters an ingest client adapter on this path so detachFromFilterChain() can
  // detach all active stores on stream teardown.
  void registerClient(Client& client) { registered_clients_.insert(&client); }

  void unregisterClient(Client& client) { registered_clients_.erase(&client); }

  // Dispatcher the external buffer and writer should use for completion/watermark callbacks.
  virtual Event::Dispatcher& dispatcher() PURE;

  // Injects a non-terminal data frame into the HTTP filter chain; end-of-stream is the caller's
  // concern.
  virtual void injectData(Buffer::Instance& data) PURE;

  // Fails the stream after an unrecoverable external-buffer error.
  virtual void onUnrecoverableError() PURE;

  // Reports bytes an ingest client has accepted but not yet made durable. The bridge sums them
  // across clients and pauses the data source once the total reaches high_watermark_.
  void addUnacked(uint64_t bytes);

  // Reports bytes that have since become durable, resuming the data source once the total falls to
  // low_watermark_.
  void releaseUnacked(uint64_t bytes);

  // Move-only RAII token that charges `bytes` to `addUnacked()` on construction and releases them
  // via `releaseUnacked()` on destruction.
  class ScopedUnacked {
  public:
    ScopedUnacked(FilterChainBridge& bridge, uint64_t bytes) : bridge_(&bridge), bytes_(bytes) {
      if (bytes_ > 0) {
        bridge_->addUnacked(bytes_);
      }
    }
    ScopedUnacked(ScopedUnacked&& other) noexcept
        : bridge_(std::exchange(other.bridge_, nullptr)), bytes_(std::exchange(other.bytes_, 0)) {}
    ScopedUnacked& operator=(ScopedUnacked&&) = delete;
    ScopedUnacked(const ScopedUnacked&) = delete;
    ScopedUnacked& operator=(const ScopedUnacked&) = delete;
    ~ScopedUnacked() {
      if (bridge_ != nullptr && bytes_ > 0) {
        bridge_->releaseUnacked(bytes_);
      }
    }

  private:
    FilterChainBridge* bridge_{nullptr};
    uint64_t bytes_{0};
  };

  // True while the data source is paused.
  bool ingestPaused() const { return source_paused_; }

  // Registers a one-shot callback invoked when write back-pressure drains (`writePaused()` returns
  // to false). Only one writer on a path injects at a time.
  void setWriteResumeCallback(WriteResumeCallback cb) {
    ASSERT(write_resume_cb_ == nullptr);
    write_resume_cb_ = std::move(cb);
  }

  // Clears any pending write-resume callback.
  void clearWriteResumeCallback() { write_resume_cb_ = nullptr; }

  // Stops watermark delivery. Must be called on or before the filter's onDestroy(): FilterManager
  // destroys its watermark callback lists before it destroys the filters that registered with
  // them, so unsubscribing from the bridge's destructor is too late.
  void detachFromFilterChain();

  // True while the filter chain we inject into is above its write high watermark.
  bool writePaused() const { return write_high_watermark_count_ > 0; }

  // Bounds the chunks injected in one event-loop pass, across every writer on the path. Returns
  // false once the budget is spent, at which point the caller must stop injecting and resume on a
  // later pass. Synchronous reads or serialization bursts can otherwise drive the inject loop
  // back-to-back, monopolizing the worker thread and starving other connections and timers. The
  // budget also caps that loop's recursion depth.
  bool tryConsumeInjectBudget();

  // Refills the budget. Called by a writer re-entering the inject loop on a later pass than the
  // one that spent it -- never on a same-pass continuation, which would defeat the cap.
  void resetInjectBudget();

protected:
  // Entry points the adapter calls from the path's Envoy watermark callbacks.
  void onAboveWriteWatermark();

  void onBelowWriteWatermark();

private:
  // Pushes ingest back-pressure toward the data source (the filter's own write-buffer high/low
  // watermark on this path).
  virtual void pauseSource() PURE;

  virtual void resumeSource() PURE;

  // Unsubscribes *this from the path's Envoy watermark callbacks. The adapter subscribes in its
  // constructor, for the bridge's whole life.
  virtual void unsubscribeWriteWatermarks() PURE;

  void updateIngestBackpressure();

  // Chunks that can be injected in one event-loop pass (see tryConsumeInjectBudget).
  static constexpr uint32_t InjectChunksPerIteration = 8;

  uint32_t inject_budget_used_{0};

  // Bytes accepted by any store on this path that are not yet durable.
  uint64_t unacked_{0};
  const uint32_t high_watermark_;
  const uint32_t low_watermark_;
  // Tracked so each crossing pauses or resumes the source exactly once.
  bool source_paused_{false};
  bool detached_{false};

  // Depth of unmatched write high-watermark callbacks. The connection manager may raise the
  // watermark more than once (stream and connection), so injection resumes only when this returns
  // to zero.
  uint32_t write_high_watermark_count_{0};
  // When pauseSource() or resumeSource() reports an ingest buffer watermark to Envoy, Envoy fans
  // that same signal back to every watermark subscriber on the stream, including this bridge.
  // These counters track how many self-triggered callbacks are currently in flight on the stack so
  // onAboveWriteWatermark() and onBelowWriteWatermark() can ignore our own echoes instead of
  // mistaking ingest back-pressure for write back-pressure.
  uint32_t expected_high_watermark_echoes_{0};
  uint32_t expected_low_watermark_echoes_{0};
  WriteResumeCallback write_resume_cb_;
  absl::flat_hash_set<Client*> registered_clients_;
};
using FilterChainBridgePtr = std::unique_ptr<FilterChainBridge>;

// FilterChainBridge for the decode (request) path. Maps the path-agnostic bridge methods onto
// StreamDecoderFilterCallbacks, and forwards upstream-request watermarks into the bridge's write
// flow control.
class DecoderFilterChainBridge : public FilterChainBridge, public Http::UpstreamWatermarkCallbacks {
public:
  // Subscribes for the bridge's whole life rather than per write: the filter manager replays the
  // current high-watermark depth to every new subscriber, so re-subscribing double-counts.
  DecoderFilterChainBridge(Http::StreamDecoderFilterCallbacks& callbacks,
                           AiProtocolManagerStats& stats)
      : FilterChainBridge(callbacks.decoderBufferLimit()), callbacks_(callbacks), stats_(stats) {
    callbacks_.addUpstreamWatermarkCallbacks(*this);
  }

  ~DecoderFilterChainBridge() override { detachFromFilterChain(); }

  // FilterChainBridge
  Event::Dispatcher& dispatcher() override { return callbacks_.dispatcher(); }
  void injectData(Buffer::Instance& data) override {
    callbacks_.injectDecodedDataToFilterChain(data, /*end_stream=*/false);
  }
  void onUnrecoverableError() override;

  // Http::UpstreamWatermarkCallbacks (write side: upstream back-pressure).
  void onAboveWriteBufferHighWatermark() override { onAboveWriteWatermark(); }
  void onBelowWriteBufferLowWatermark() override { onBelowWriteWatermark(); }

private:
  // FilterChainBridge
  void pauseSource() override { callbacks_.onDecoderFilterAboveWriteBufferHighWatermark(); }
  void resumeSource() override { callbacks_.onDecoderFilterBelowWriteBufferLowWatermark(); }
  void unsubscribeWriteWatermarks() override { callbacks_.removeUpstreamWatermarkCallbacks(*this); }

  Http::StreamDecoderFilterCallbacks& callbacks_;
  AiProtocolManagerStats& stats_;
};

// FilterChainBridge for the encode (response) path. Maps the bridge methods onto
// StreamEncoderFilterCallbacks, but uses the decoder callbacks to subscribe to downstream
// watermarks (add/removeDownstreamWatermarkCallbacks live on StreamDecoderFilterCallbacks).
class EncoderFilterChainBridge : public FilterChainBridge,
                                 public Http::DownstreamWatermarkCallbacks {
public:
  EncoderFilterChainBridge(Http::StreamEncoderFilterCallbacks& encoder_callbacks,
                           Http::StreamDecoderFilterCallbacks& decoder_callbacks,
                           AiProtocolManagerStats& stats)
      : FilterChainBridge(encoder_callbacks.encoderBufferLimit()),
        encoder_callbacks_(encoder_callbacks), decoder_callbacks_(decoder_callbacks),
        stats_(stats) {
    decoder_callbacks_.addDownstreamWatermarkCallbacks(*this);
  }

  ~EncoderFilterChainBridge() override { detachFromFilterChain(); }

  // FilterChainBridge
  Event::Dispatcher& dispatcher() override { return encoder_callbacks_.dispatcher(); }
  void injectData(Buffer::Instance& data) override {
    encoder_callbacks_.injectEncodedDataToFilterChain(data, /*end_stream=*/false);
  }
  void onUnrecoverableError() override;

  // Http::DownstreamWatermarkCallbacks (write side: downstream back-pressure).
  void onAboveWriteBufferHighWatermark() override { onAboveWriteWatermark(); }
  void onBelowWriteBufferLowWatermark() override { onBelowWriteWatermark(); }

private:
  // FilterChainBridge
  void pauseSource() override { encoder_callbacks_.onEncoderFilterAboveWriteBufferHighWatermark(); }
  void resumeSource() override { encoder_callbacks_.onEncoderFilterBelowWriteBufferLowWatermark(); }
  void unsubscribeWriteWatermarks() override {
    decoder_callbacks_.removeDownstreamWatermarkCallbacks(*this);
  }

  Http::StreamEncoderFilterCallbacks& encoder_callbacks_;
  Http::StreamDecoderFilterCallbacks& decoder_callbacks_;
  AiProtocolManagerStats& stats_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
