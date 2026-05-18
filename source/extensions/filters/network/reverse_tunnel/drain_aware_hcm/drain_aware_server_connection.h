#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/http/codec.h"
#include "envoy/network/drain_decision.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

// Wraps an Http::ServerConnection and proactively sends an HTTP/2 GOAWAY frame when
// the listener that owns this connection begins draining. Drain is detected by polling
// DrainDecision::drainClose() on a short timer; this avoids calling addOnDrainCloseCb()
// which is intentionally unsupported on PerFilterChainFactoryContextImpl.
class DrainAwareServerConnection : public Http::ServerConnection,
                                   public Logger::Loggable<Logger::Id::filter> {
public:
  DrainAwareServerConnection(Http::ServerConnectionPtr inner, Event::Dispatcher& dispatcher,
                             const Network::DrainDecision& drain_decision)
      : inner_(std::move(inner)), drain_decision_(drain_decision) {
    ENVOY_LOG(debug, "drain_aware_hcm: created server connection wrapper, protocol={}",
              static_cast<int>(inner_->protocol()));
    drain_check_timer_ = dispatcher.createTimer([this]() { onDrainCheckTimer(); });
    drain_check_timer_->enableTimer(std::chrono::milliseconds(100));
  }

  ~DrainAwareServerConnection() override {
    if (drain_check_timer_ != nullptr) {
      drain_check_timer_->disableTimer();
    }
  }

  Http::Status dispatch(Buffer::Instance& data) override { return inner_->dispatch(data); }
  void goAway() override { inner_->goAway(); }
  Http::Protocol protocol() override { return inner_->protocol(); }
  void shutdownNotice() override { inner_->shutdownNotice(); }
  bool wantsToWrite() override { return inner_->wantsToWrite(); }

  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override {
    inner_->onUnderlyingConnectionAboveWriteBufferHighWatermark();
  }

  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override {
    inner_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
  }

private:
  void onDrainCheckTimer() {
    if (drain_goaway_sent_) {
      return;
    }
    if (drain_decision_.drainClose(Network::DrainDirection::All)) {
      ENVOY_LOG(info, "drain_aware_hcm: drain detected, sending GOAWAY");
      drain_goaway_sent_ = true;
      inner_->goAway();
      return;
    }
    drain_check_timer_->enableTimer(std::chrono::milliseconds(100));
  }

  Http::ServerConnectionPtr inner_;
  const Network::DrainDecision& drain_decision_;
  Event::TimerPtr drain_check_timer_;
  bool drain_goaway_sent_{false};
};

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
