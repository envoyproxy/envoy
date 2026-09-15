#pragma once

#include "envoy/http/filter.h"

#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/stats.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// FilterChainBridge for the decode (request) path. Maps the path-agnostic bridge
// methods onto StreamDecoderFilterCallbacks, and forwards upstream-request
// watermarks into the bridge's replay flow control.
class DecoderFilterChainBridge : public FilterChainBridge, public Http::UpstreamWatermarkCallbacks {
public:
  // Subscribes for the bridge's whole life rather than per replay: the filter manager replays the
  // current high-watermark depth to every new subscriber, so re-subscribing double-counts.
  DecoderFilterChainBridge(Http::StreamDecoderFilterCallbacks& callbacks,
                           AiProtocolManagerStats& stats)
      : FilterChainBridge(callbacks.decoderBufferLimit()), callbacks_(callbacks), stats_(stats) {
    callbacks_.addUpstreamWatermarkCallbacks(*this);
  }

  // FilterChainBridge
  Event::Dispatcher& dispatcher() override { return callbacks_.dispatcher(); }
  void injectData(Buffer::Instance& data) override {
    callbacks_.injectDecodedDataToFilterChain(data, /*end_stream=*/false);
  }
  void onUnrecoverableError() override;

  // Http::UpstreamWatermarkCallbacks (replay side: upstream back-pressure).
  void onAboveWriteBufferHighWatermark() override { onAboveReplayWatermark(); }
  void onBelowWriteBufferLowWatermark() override { onBelowReplayWatermark(); }

private:
  // FilterChainBridge
  void pauseSource() override { callbacks_.onDecoderFilterAboveWriteBufferHighWatermark(); }
  void resumeSource() override { callbacks_.onDecoderFilterBelowWriteBufferLowWatermark(); }
  void unsubscribeReplayWatermarks() override {
    callbacks_.removeUpstreamWatermarkCallbacks(*this);
  }

  Http::StreamDecoderFilterCallbacks& callbacks_;
  AiProtocolManagerStats& stats_;
};

// FilterChainBridge for the encode (response) path. Maps the bridge methods onto
// StreamEncoderFilterCallbacks, but uses the decoder callbacks to subscribe to
// downstream watermarks (add/removeDownstreamWatermarkCallbacks live on
// StreamDecoderFilterCallbacks).
//
// Provided so the encode path is trivial to wire later; not yet constructed by
// the filter.
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

  // FilterChainBridge
  Event::Dispatcher& dispatcher() override { return encoder_callbacks_.dispatcher(); }
  void injectData(Buffer::Instance& data) override {
    encoder_callbacks_.injectEncodedDataToFilterChain(data, /*end_stream=*/false);
  }
  void onUnrecoverableError() override;

  // Http::DownstreamWatermarkCallbacks (replay side: downstream back-pressure).
  void onAboveWriteBufferHighWatermark() override { onAboveReplayWatermark(); }
  void onBelowWriteBufferLowWatermark() override { onBelowReplayWatermark(); }

private:
  // FilterChainBridge
  void pauseSource() override { encoder_callbacks_.onEncoderFilterAboveWriteBufferHighWatermark(); }
  void resumeSource() override { encoder_callbacks_.onEncoderFilterBelowWriteBufferLowWatermark(); }
  void unsubscribeReplayWatermarks() override {
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
