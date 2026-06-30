#pragma once

#include "envoy/http/filter.h"

#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// FilterChainBridge for the decode (request) path. Maps the path-agnostic bridge
// methods onto StreamDecoderFilterCallbacks, and forwards upstream-request
// watermarks into the BufferManager's ReplayWatermarkHandler so replay is paced
// against upstream back-pressure.
class DecoderFilterChainBridge : public FilterChainBridge, public Http::UpstreamWatermarkCallbacks {
public:
  explicit DecoderFilterChainBridge(Http::StreamDecoderFilterCallbacks& callbacks)
      : callbacks_(callbacks) {}

  // FilterChainBridge
  Event::Dispatcher& dispatcher() override { return callbacks_.dispatcher(); }
  uint32_t bufferLimit() override { return callbacks_.decoderBufferLimit(); }
  void injectData(Buffer::Instance& data) override {
    callbacks_.injectDecodedDataToFilterChain(data, /*end_stream=*/false);
  }
  void pauseSource() override { callbacks_.onDecoderFilterAboveWriteBufferHighWatermark(); }
  void resumeSource() override { callbacks_.onDecoderFilterBelowWriteBufferLowWatermark(); }
  void registerReplayWatermarks(ReplayWatermarkHandler& handler) override;
  void unregisterReplayWatermarks() override;
  void onUnrecoverableError() override;

  // Http::UpstreamWatermarkCallbacks (replay side: upstream back-pressure).
  void onAboveWriteBufferHighWatermark() override {
    if (handler_ != nullptr) {
      handler_->onReplayAboveHighWatermark();
    }
  }
  void onBelowWriteBufferLowWatermark() override {
    if (handler_ != nullptr) {
      handler_->onReplayBelowLowWatermark();
    }
  }

private:
  Http::StreamDecoderFilterCallbacks& callbacks_;
  ReplayWatermarkHandler* handler_{nullptr};
  // Whether *this is currently registered as an UpstreamWatermarkCallbacks.
  bool registered_{false};
};

// FilterChainBridge for the encode (response) path. Maps the bridge methods onto
// StreamEncoderFilterCallbacks, but uses the decoder callbacks to subscribe to
// downstream watermarks (add/removeDownstreamWatermarkCallbacks live on
// StreamDecoderFilterCallbacks), forwarding them into the BufferManager's
// ReplayWatermarkHandler.
//
// Provided so the encode path is trivial to wire later; not yet constructed by
// the filter.
class EncoderFilterChainBridge : public FilterChainBridge,
                                 public Http::DownstreamWatermarkCallbacks {
public:
  EncoderFilterChainBridge(Http::StreamEncoderFilterCallbacks& encoder_callbacks,
                           Http::StreamDecoderFilterCallbacks& decoder_callbacks)
      : encoder_callbacks_(encoder_callbacks), decoder_callbacks_(decoder_callbacks) {}

  // FilterChainBridge
  Event::Dispatcher& dispatcher() override { return encoder_callbacks_.dispatcher(); }
  uint32_t bufferLimit() override { return encoder_callbacks_.encoderBufferLimit(); }
  void injectData(Buffer::Instance& data) override {
    encoder_callbacks_.injectEncodedDataToFilterChain(data, /*end_stream=*/false);
  }
  void pauseSource() override { encoder_callbacks_.onEncoderFilterAboveWriteBufferHighWatermark(); }
  void resumeSource() override { encoder_callbacks_.onEncoderFilterBelowWriteBufferLowWatermark(); }
  void registerReplayWatermarks(ReplayWatermarkHandler& handler) override;
  void unregisterReplayWatermarks() override;
  void onUnrecoverableError() override;

  // Http::DownstreamWatermarkCallbacks (replay side: downstream back-pressure).
  void onAboveWriteBufferHighWatermark() override {
    if (handler_ != nullptr) {
      handler_->onReplayAboveHighWatermark();
    }
  }
  void onBelowWriteBufferLowWatermark() override {
    if (handler_ != nullptr) {
      handler_->onReplayBelowLowWatermark();
    }
  }

private:
  Http::StreamEncoderFilterCallbacks& encoder_callbacks_;
  Http::StreamDecoderFilterCallbacks& decoder_callbacks_;
  ReplayWatermarkHandler* handler_{nullptr};
  // Whether *this is currently registered as a DownstreamWatermarkCallbacks.
  bool registered_{false};
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
