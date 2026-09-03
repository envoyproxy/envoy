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
// through the captured ReplayWatermarkHandler, exactly as a real decoder/encoder
// bridge would when the connection manager raises a watermark.
class FakeBridge : public FilterChainBridge {
public:
  explicit FakeBridge(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  uint32_t bufferLimit() override { return buffer_limit_; }
  void injectData(Buffer::Instance& data) override {
    injected_.add(data);
    ++inject_calls_;
    if (on_inject_ != nullptr) {
      on_inject_();
    }
    // Simulate downstream back-pressure arising mid-replay: when configured, raise
    // the replay high watermark right after the Nth injected chunk, as a real
    // chain would when its write buffer fills.
    if (handler_ != nullptr && inject_calls_ == raise_replay_watermark_at_inject_) {
      handler_->onReplayAboveHighWatermark();
    }
  }
  void pauseSource() override { ++pause_source_calls_; }
  void resumeSource() override { ++resume_source_calls_; }
  void registerReplayWatermarks(ReplayWatermarkHandler& handler) override { handler_ = &handler; }
  void unregisterReplayWatermarks() override { handler_ = nullptr; }
  void onUnrecoverableError() override { ++error_calls_; }

  Event::Dispatcher& dispatcher_;
  uint32_t buffer_limit_{1024 * 1024};
  ReplayWatermarkHandler* handler_{nullptr};

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
