#pragma once

#include <functional>

#include "envoy/event/dispatcher.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Hand-written FilterChainBridge that records everything ingest managers, ChainWriter, and
// FilterManager do to the (notional) filter chain, so path-agnostic ingestion and emission logic
// can be unit-tested without HTTP filter mocks. Tests drive write back-pressure through
// raiseWriteWatermark()/lowerWriteWatermark(), matching a real decoder/encoder bridge when the
// connection manager raises a watermark.
class FakeBridge : public FilterChainBridge {
public:
  // The bridge samples the ingest buffer limit at construction, so a test that exercises ingest
  // back-pressure passes its own.
  explicit FakeBridge(Event::Dispatcher& dispatcher, uint32_t buffer_limit = 1024 * 1024)
      : FilterChainBridge(buffer_limit), dispatcher_(dispatcher) {}

  ~FakeBridge() override { detachFromFilterChain(); }

  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  void injectData(Buffer::Instance& data) override {
    injected_.add(data);
    ++inject_calls_;
    if (on_inject_ != nullptr) {
      on_inject_();
    }
    // Simulate downstream/upstream back-pressure arising mid-write: when configured, raise the
    // write high watermark right after the Nth injected chunk, as a real chain would when its
    // write buffer fills.
    if (subscribed_ && inject_calls_ == raise_write_watermark_at_inject_) {
      onAboveWriteWatermark();
    }
  }
  void pauseSource() override { ++pause_source_calls_; }
  void resumeSource() override {
    ++resume_source_calls_;
    if (on_resume_source_ != nullptr) {
      on_resume_source_();
    }
  }
  void unsubscribeWriteWatermarks() override { subscribed_ = false; }
  void onUnrecoverableError() override {
    ++error_calls_;
    if (on_error_ != nullptr) {
      on_error_();
    }
  }

  // Drives write back-pressure as the connection manager would.
  void raiseWriteWatermark() { onAboveWriteWatermark(); }
  void lowerWriteWatermark() { onBelowWriteWatermark(); }

  Event::Dispatcher& dispatcher_;
  // A real adapter subscribes to the path's watermarks in its constructor.
  bool subscribed_{true};

  Buffer::OwnedImpl injected_;
  int inject_calls_{0};
  int pause_source_calls_{0};
  int resume_source_calls_{0};
  int error_calls_{0};
  int raise_write_watermark_at_inject_{0}; // 0 = never.
  std::function<void()> on_inject_;
  std::function<void()> on_error_;
  std::function<void()> on_resume_source_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
