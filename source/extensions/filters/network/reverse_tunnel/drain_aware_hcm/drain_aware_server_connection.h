#pragma once

#include <functional>
#include <memory>
#include <utility>

#include "envoy/event/dispatcher.h"
#include "envoy/http/codec.h"
#include "envoy/network/drain_decision.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

// Interposes between the HTTP/2 server codec and the HCM's ServerConnectionCallbacks to observe a
// peer GOAWAY, which the default server path ignores (ConnectionManagerImpl::onGoAway is a no-op
// for servers). For reverse tunnels we treat it as "this tunnel is going away" and dial a
// replacement immediately, while the old tunnel serves in-flight streams until the peer's final
// GOAWAY closes it. All other callbacks are forwarded unchanged.
class DrainAwareServerConnectionCallbacks : public Http::ServerConnectionCallbacks,
                                            public Logger::Loggable<Logger::Id::filter> {
public:
  DrainAwareServerConnectionCallbacks(Http::ServerConnectionCallbacks& inner,
                                      std::function<void()> on_peer_goaway)
      : inner_(inner), on_peer_goaway_(std::move(on_peer_goaway)) {}

  // Http::ServerConnectionCallbacks
  Http::RequestDecoder& newStream(Http::ResponseEncoder& response_encoder,
                                  bool is_internally_created = false) override {
    return inner_.newStream(response_encoder, is_internally_created);
  }

  // Http::ConnectionCallbacks
  void onGoAway(Http::GoAwayErrorCode error_code) override {
    // Envoy's codec only delivers the first GOAWAY, but guard anyway so a re-dial fires at most
    // once per tunnel from this path.
    if (!peer_goaway_handled_ && on_peer_goaway_ != nullptr) {
      peer_goaway_handled_ = true;
      ENVOY_LOG(info,
                "drain_aware_hcm: peer GOAWAY for connection (code={}); draining tunnel and "
                "dialing replacement",
                static_cast<int>(error_code));
      on_peer_goaway_();
    }
    inner_.onGoAway(error_code);
  }
  void onSettings(Http::ReceivedSettings& settings) override { inner_.onSettings(settings); }
  void onMaxStreamsChanged(uint32_t num_streams) override {
    inner_.onMaxStreamsChanged(num_streams);
  }

private:
  Http::ServerConnectionCallbacks& inner_;
  std::function<void()> on_peer_goaway_;
  bool peer_goaway_handled_{false};
};

// Wraps an Http::ServerConnection and proactively sends an HTTP/2 GOAWAY frame when
// the listener that owns this connection begins draining. Drain is detected by polling
// DrainDecision::drainClose() on a short timer; this avoids calling addOnDrainCloseCb()
// which is intentionally unsupported on PerFilterChainFactoryContextImpl.
class DrainAwareServerConnection : public Http::ServerConnection,
                                   public Logger::Loggable<Logger::Id::filter> {
public:
  // `on_local_drain` (optional) fires once when this connection begins draining locally (the HCM
  // sends a shutdownNotice due to max_connection_duration/graceful shutdown, or the listener
  // drains). For reverse tunnels this asks the initiator to dial a replacement tunnel immediately
  // while the old one finishes in-flight streams.
  DrainAwareServerConnection(
      Http::ServerConnectionPtr inner, Event::Dispatcher& dispatcher,
      const Network::DrainDecision& drain_decision, std::function<void()> on_local_drain = nullptr,
      std::unique_ptr<DrainAwareServerConnectionCallbacks> callbacks_wrapper = nullptr)
      : callbacks_wrapper_(std::move(callbacks_wrapper)), inner_(std::move(inner)),
        drain_decision_(drain_decision), on_local_drain_(std::move(on_local_drain)) {
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
  void shutdownNotice() override {
    // The HCM calls this at the start of a graceful drain (e.g. max_connection_duration). For
    // reverse tunnels (on_local_drain_ set) we use it as the "tunnel draining" signal to dial a
    // replacement now, but SUPPRESS the early GOAWAY so the peer keeps using this tunnel during the
    // grace window. The HCM's final GOAWAY at drain_timeout (via goAway()) then migrates new
    // requests to the established replacement while in-flight requests finish here.
    if (on_local_drain_ != nullptr) {
      notifyLocalDrain();
      return;
    }
    inner_->shutdownNotice();
  }
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
      notifyLocalDrain();
      inner_->goAway();
      return;
    }
    drain_check_timer_->enableTimer(std::chrono::milliseconds(100));
  }

  // Fires the local-drain callback at most once.
  void notifyLocalDrain() {
    if (local_drain_notified_ || on_local_drain_ == nullptr) {
      return;
    }
    local_drain_notified_ = true;
    on_local_drain_();
  }

  // Declared before inner_ so the codec (which holds a reference to this wrapper) is destroyed
  // before the wrapper. Null when the peer-GOAWAY re-dial path is disabled.
  std::unique_ptr<DrainAwareServerConnectionCallbacks> callbacks_wrapper_;
  Http::ServerConnectionPtr inner_;
  const Network::DrainDecision& drain_decision_;
  std::function<void()> on_local_drain_;
  Event::TimerPtr drain_check_timer_;
  bool drain_goaway_sent_{false};
  bool local_drain_notified_{false};
};

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
