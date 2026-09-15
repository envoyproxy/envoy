#pragma once

#include <functional>

#include "envoy/event/dispatcher.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Hand-written FilterChainBridge that records everything the BufferManager / FilterManager does
// to the (notional) filter chain, so the path-agnostic offload/replay logic can
// be unit-tested without any HTTP filter mocks. Tests drive replay back-pressure
// through raiseReplayWatermark()/lowerReplayWatermark(), exactly as a real
// decoder/encoder bridge would when the connection manager raises a watermark.
class FakeBridge : public FilterChainBridge {
public:
  // The bridge samples the ingest buffer limit at construction, so a test that exercises ingest
  // back-pressure passes its own.
  explicit FakeBridge(Event::Dispatcher& dispatcher, uint32_t buffer_limit = 1024 * 1024)
      : FilterChainBridge(buffer_limit), dispatcher_(dispatcher) {}

  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  void injectData(Buffer::Instance& data) override {
    injected_.add(data);
    ++inject_calls_;
    if (on_inject_ != nullptr) {
      on_inject_();
    }
    // Simulate downstream back-pressure arising mid-replay: when configured, raise
    // the replay high watermark right after the Nth injected chunk, as a real
    // chain would when its write buffer fills.
    if (subscribed_ && inject_calls_ == raise_replay_watermark_at_inject_) {
      onAboveReplayWatermark();
    }
  }
  void pauseSource() override { ++pause_source_calls_; }
  void resumeSource() override { ++resume_source_calls_; }
  void unsubscribeReplayWatermarks() override { subscribed_ = false; }
  void onUnrecoverableError() override { ++error_calls_; }

  // Drives replay back-pressure as the connection manager would.
  void raiseReplayWatermark() { onAboveReplayWatermark(); }
  void lowerReplayWatermark() { onBelowReplayWatermark(); }

  Event::Dispatcher& dispatcher_;
  // A real adapter subscribes to the path's watermarks in its constructor.
  bool subscribed_{true};

  Buffer::OwnedImpl injected_;
  int inject_calls_{0};
  int pause_source_calls_{0};
  int resume_source_calls_{0};
  int error_calls_{0};
  int raise_replay_watermark_at_inject_{0}; // 0 = never.
  std::function<void()> on_inject_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
