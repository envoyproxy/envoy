#pragma once

#include <chrono>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/http/codec.h"
#include "envoy/network/drain_decision.h"

#include "source/common/common/logger.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/downstream_reverse_connection_io_handle.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

// Wraps an Http::ServerConnection to manage the drain sequence for reverse tunnels.
//
// Key idea: we NEVER forward the HCM's phase-1 shutdownNotice() GOAWAY. The only GOAWAY ever put
// on the wire is the final goAway(), and it is guarded so it is sent at most once. Two bools track
// the two halves of the sequence: drain_initiated_ (shutdownNotice handled) and
// drain_goaway_sent_ (final GOAWAY emitted).
//
// Two drain triggers:
//
// 1. Connection-level drain (max_connection_duration, max_requests, ...): the HCM calls
//    shutdownNotice() then goAway() after drain_timeout. We swallow shutdownNotice() and instead
//    proactively establish a REPLACEMENT reverse tunnel (when the listener is healthy), so the
//    cluster has somewhere to route. The old connection keeps serving during drain_timeout
//    because no GOAWAY has been sent yet. The HCM's goAway() then sends the single final GOAWAY.
//
// 2. Listener-level drain (hot-restart, /drain_listeners, healthcheck fail): the HCM only checks
//    drainClose() while encoding a response, so it never notices listener drain on an idle
//    connection. We poll drainClose() on a short timer; on detection we do NOT reconnect (the
//    listener is dying) and instead arm a timer to send the final goAway() after drain_timeout,
//    giving any in-flight request time to finish before the connection closes.
//
// drain_goaway_sent_ guards goAway() so the two paths (HCM-driven and our proactive timer) can
// never put a duplicate GOAWAY on the wire.
class DrainAwareServerConnection : public Http::ServerConnection,
                                   public Logger::Loggable<Logger::Id::filter> {
public:
  // @param drain_decision listener-level drain decision; polled to detect listener drain.
  // @param io_handle reference to the connection's IoHandle; used to trigger reconnection on a
  //        connection-level drain. Lifetime is guaranteed by the Network::Connection that owns
  //        both this codec wrapper and the IoHandle.
  // @param dispatcher the connection's dispatcher; owns the drain-poll and proactive-goaway timers.
  // @param drain_timeout gap between detecting a listener drain and sending the final goAway when
  //        we drive the sequence ourselves. Mirrors the HCM's drain_timeout so the graceful window
  //        is identical to a normal HCM-driven drain.
  DrainAwareServerConnection(Http::ServerConnectionPtr inner,
                             const Network::DrainDecision& drain_decision,
                             Network::IoHandle& io_handle, Event::Dispatcher& dispatcher,
                             std::chrono::milliseconds drain_timeout)
      : inner_(std::move(inner)), drain_decision_(drain_decision), io_handle_(io_handle),
        drain_timeout_(drain_timeout) {
    ENVOY_LOG(debug, "drain_aware_hcm: created server connection wrapper, protocol={}",
              static_cast<int>(inner_->protocol()));
    drain_check_timer_ = dispatcher.createTimer([this]() { onDrainCheckTimer(); });
    proactive_goaway_timer_ = dispatcher.createTimer([this]() { goAway(); });
    drain_check_timer_->enableTimer(kDrainPollInterval);
  }

  Http::Status dispatch(Buffer::Instance& data) override { return inner_->dispatch(data); }

  // Sends the final GOAWAY exactly once. Reached either from the HCM's own drain sequence
  // (connection-level drain) or from proactive_goaway_timer_ (listener-level drain on an idle
  // connection). The bool guard makes the two paths mutually idempotent.
  void goAway() override {
    if (drain_goaway_sent_) {
      return;
    }
    drain_goaway_sent_ = true;
    inner_->goAway();
  }

  Http::Protocol protocol() override { return inner_->protocol(); }

  // The HCM's phase-1 drain notice. We always swallow it (never forward to the inner codec) so no
  // premature GOAWAY is emitted; instead we set up a replacement tunnel when appropriate.
  void shutdownNotice() override {
    if (drain_initiated_) {
      return;
    }
    drain_initiated_ = true;

    if (drain_decision_.drainClose(Network::DrainDirection::All)) {
      // The listener itself is draining: there is no point opening a replacement connection to a
      // dying listener. Some other listener/process is responsible for new connections; just give
      // the in-flight work time to finish and let the final goAway() close this one.
      return;
    }

    // Connection-level drain while the listener is healthy. We swallowed the phase-1 GOAWAY above,
    // so the old connection keeps accepting requests during drain_timeout. Proactively open a
    // replacement reverse tunnel so the cluster has somewhere to route once this one closes; the
    // HCM's own goAway() (after drain_timeout) sends the single final GOAWAY.
    // NOTE: dynamic_cast is used here because IoHandle does not expose a reconnection interface.
    // This coupling is acceptable since drain_aware_hcm is exclusively used with reverse tunnels.
    if (auto* io_handle = dynamic_cast<
            Extensions::Bootstrap::ReverseConnection::DownstreamReverseConnectionIOHandle*>(
            &io_handle_)) {
      io_handle->reestablishConnection();
    }
  }

  bool wantsToWrite() override { return inner_->wantsToWrite(); }

  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override {
    inner_->onUnderlyingConnectionAboveWriteBufferHighWatermark();
  }

  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override {
    inner_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
  }

private:
  // Poll interval for detecting listener-level drain on idle connections. 100ms is short enough
  // that an idle reverse tunnel reacts to a hot-restart/drain almost immediately, while being
  // cheap enough to run per-connection (one timer firing 10x/sec).
  static constexpr std::chrono::milliseconds kDrainPollInterval{100};

  // Polls the listener drain decision. Only matters for idle connections, where the HCM never
  // checks drainClose() itself. On detection, schedules the final goAway() after drain_timeout.
  void onDrainCheckTimer() {
    if (drain_initiated_) {
      return;
    }
    if (drain_decision_.drainClose(Network::DrainDirection::All)) {
      ENVOY_LOG(info, "drain_aware_hcm: listener drain detected on idle connection, scheduling "
                      "graceful goAway");
      drain_initiated_ = true;
      // No reconnection (listener is dying). Send the final GOAWAY after drain_timeout so any
      // in-flight request gets the same graceful window as a normal HCM-driven drain.
      proactive_goaway_timer_->enableTimer(drain_timeout_);
      return;
    }
    drain_check_timer_->enableTimer(kDrainPollInterval);
  }

  Http::ServerConnectionPtr inner_;
  const Network::DrainDecision& drain_decision_;
  // Underlying IoHandle for the reverse connection. Used to trigger reconnection on a
  // connection-level drain.
  Network::IoHandle& io_handle_;
  // Mirrors the HCM drain_timeout: the gap between detecting a drain and sending the final goAway
  // when this wrapper drives the sequence itself.
  const std::chrono::milliseconds drain_timeout_;
  Event::TimerPtr drain_check_timer_;
  Event::TimerPtr proactive_goaway_timer_;

  // Set once a drain has been initiated (HCM-driven shutdownNotice or polling-detected listener
  // drain), so the drain sequence is started at most once.
  bool drain_initiated_{false};
  // Set once the final GOAWAY has been emitted, so it is never sent twice.
  bool drain_goaway_sent_{false};
};

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
