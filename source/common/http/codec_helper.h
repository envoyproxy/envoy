#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/codec.h"

#include "source/common/common/assert.h"

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Http {

class StreamCallbackHelper {
public:
  void runLowWatermarkCallbacks() {
    if (reset_callbacks_started_ || local_end_stream_) {
      return;
    }
    ASSERT(high_watermark_callbacks_ > 0);
    --high_watermark_callbacks_;
    for (StreamCallbacks* callbacks : callbacks_) {
      if (callbacks) {
        callbacks->onBelowWriteBufferLowWatermark();
      }
    }
  }

  void runHighWatermarkCallbacks() {
    if (reset_callbacks_started_ || local_end_stream_) {
      return;
    }
    ++high_watermark_callbacks_;
    for (StreamCallbacks* callbacks : callbacks_) {
      if (callbacks) {
        callbacks->onAboveWriteBufferHighWatermark();
      }
    }
  }

  void runResetCallbacks(StreamResetReason reason, absl::string_view details) {
    // Reset callbacks are a special case, and the only StreamCallbacks allowed
    // to run after local_end_stream_.
    if (reset_callbacks_started_) {
      return;
    }

    reset_callbacks_started_ = true;
    for (StreamCallbacks* callbacks : callbacks_) {
      if (callbacks) {
        callbacks->onResetStream(reason, details);
      }
    }
  }

  bool local_end_stream_{};

protected:
  void addCallbacksHelper(StreamCallbacks& callbacks) {
    ASSERT(!reset_callbacks_started_ && !local_end_stream_);
    callbacks_.push_back(&callbacks);
    for (uint32_t i = 0; i < high_watermark_callbacks_; ++i) {
      callbacks.onAboveWriteBufferHighWatermark();
    }
  }

  void removeCallbacksHelper(StreamCallbacks& callbacks) {
    // For performance reasons we just clear the callback and do not resize the vector.
    // Reset callbacks scale with the number of filters per request and do not get added and
    // removed multiple times.
    // The vector may not be safely resized without making sure the run.*Callbacks() helper
    // functions above still handle removeCallbacksHelper() calls mid-loop.
    for (auto& callback : callbacks_) {
      if (callback == &callbacks) {
        callback = nullptr;
        return;
      }
    }
  }

private:
  absl::InlinedVector<StreamCallbacks*, 8> callbacks_;
  bool reset_callbacks_started_{};
  uint32_t high_watermark_callbacks_{};
};

// A base class shared between Http2 codec and Http3 codec to set a timeout for locally ended stream
// with buffered data and register the stream adapter.
class MultiplexedStreamImplBase : public Stream, public StreamCallbackHelper {
public:
  MultiplexedStreamImplBase(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}
  ~MultiplexedStreamImplBase() override { ASSERT(stream_idle_timer_ == nullptr); }
  // TODO(mattklein123): Optimally this would be done in the destructor but there are currently
  // deferred delete lifetime issues that need sorting out if the destructor of the stream is
  // going to be able to refer to the parent connection.
  virtual void destroy() { disarmStreamIdleTimer(); }

  void onLocalEndStream() {
    ASSERT(local_end_stream_);
    if (hasPendingData()) {
      createPendingFlushTimer();
    }
  }

  void disarmStreamIdleTimer() {
    if (stream_idle_timer_ != nullptr) {
      // To ease testing and the destructor assertion.
      stream_idle_timer_->disableTimer();
      stream_idle_timer_.reset();
    }
  }

  CodecEventCallbacks* registerCodecEventCallbacks(CodecEventCallbacks* codec_callbacks) override {
    std::swap(codec_callbacks, codec_callbacks_);
    return codec_callbacks;
  }

protected:
  void setFlushTimeout(std::chrono::milliseconds timeout) override {
    stream_idle_timeout_ = timeout;
  }

  void createPendingFlushTimer() {
    ASSERT(stream_idle_timer_ == nullptr);
    if (stream_idle_timeout_.count() > 0) {
      stream_idle_timer_ = dispatcher_.createTimer([this] { onPendingFlushTimer(); });
      stream_idle_timer_->enableTimer(stream_idle_timeout_);
    }
  }

  virtual void onPendingFlushTimer() { stream_idle_timer_.reset(); }

  virtual bool hasPendingData() PURE;

  CodecEventCallbacks* codec_callbacks_{nullptr};

private:
  Event::Dispatcher& dispatcher_;
  // See HttpConnectionManager.stream_idle_timeout.
  std::chrono::milliseconds stream_idle_timeout_{};
  Event::TimerPtr stream_idle_timer_;
};

} // namespace Http
} // namespace Envoy
