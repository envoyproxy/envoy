#pragma once

#include <vector>

namespace Envoy {
namespace Http {

class StreamCallbackHelper {
public:
  void runLowWatermarkCallbacks() {
    if (reset_callbacks_started_) {
      return;
    }
    ASSERT(high_watermark_callbacks_ > 0);
    --high_watermark_callbacks_;
    // TODO(alyssawilk) see if we can make this safe for disconnects mid-loop
    for (StreamCallbacks* callbacks : callbacks_) {
      callbacks->onBelowWriteBufferLowWatermark();
    }
  }

  void runHighWatermarkCallbacks() {
    if (reset_callbacks_started_) {
      return;
    }
    ++high_watermark_callbacks_;
    for (StreamCallbacks* callbacks : callbacks_) {
      callbacks->onAboveWriteBufferHighWatermark();
    }
  }

  void runResetCallbacks(StreamResetReason reason) {
    if (reset_callbacks_run_) {
      return;
    }

    reset_callbacks_started_ = true;
    for (StreamCallbacks* callbacks : callbacks_) {
      if (callbacks) {
        callbacks->onResetStream(reason);
      }
    }

    reset_callbacks_run_ = true;
  }

protected:
  StreamCallbackHelper() {
    // Set space for 8 callbacks (64 bytes).
    callbacks_.reserve(8);
  }

  void addCallbacks_(StreamCallbacks& callbacks) {
    callbacks_.push_back(&callbacks);
    if (!reset_callbacks_run_) {
      for (uint32_t i = 0; i < high_watermark_callbacks_; ++i) {
        callbacks.onAboveWriteBufferHighWatermark();
      }
    }
  }

  void removeCallbacks_(StreamCallbacks& callbacks) {
    // For performance reasons we just clear the callback and do not resize the vector.
    // Reset callbacks scale with the number of filters per request and do not get added and
    // removed multiple times.
    for (size_t i = 0; i < callbacks_.size(); i++) {
      if (callbacks_[i] == &callbacks) {
        callbacks_[i] = nullptr;
        return;
      }
    }
  }

private:
  std::vector<StreamCallbacks*> callbacks_;
  bool reset_callbacks_run_{};
  bool reset_callbacks_started_{};
  uint32_t high_watermark_callbacks_{0};
};

} // namespace Http
} // namespace Envoy
