#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/codec.h"

#include "source/common/common/logger.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"
#include "source/extensions/upstreams/http/reverse_tunnel/drain_registry.h"
#include "source/extensions/upstreams/http/reverse_tunnel/reverse_tunnel_codec_stats.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace ReverseTunnel {

/**
 * Wraps the codec connection callbacks so the upstream (client) side observes a peer GOAWAY or a
 * drain METADATA frame: the event is forwarded to the reverse-tunnel socket manager for reporter
 * notification, then passed through to the pool's normal handling.
 */
class DrainAwareClientCallbacks : public Envoy::Http::ConnectionCallbacks,
                                  public Logger::Loggable<Logger::Id::client> {
public:
  DrainAwareClientCallbacks(Envoy::Http::ConnectionCallbacks& inner, int fd)
      : inner_(inner), fd_(fd) {}

  // Envoy::Http::ConnectionCallbacks
  void onGoAway(Envoy::Http::GoAwayErrorCode error_code) override {
    ENVOY_LOG(debug, "reverse_tunnel upstream codec: observed peer GOAWAY (error_code={})",
              static_cast<int>(error_code));
    // Early drain METADATA only notifies the reporter. Stop selecting this tunnel when the
    // GOAWAY also tells the pool to stop opening streams, even if METADATA was already reported.
    if (auto* socket_manager = Bootstrap::ReverseConnection::ReverseTunnelAcceptorExtension::
            getThreadLocalSocketManager()) {
      socket_manager->markSocketDraining(fd_);
    }
    notifyReporter();
    inner_.onGoAway(error_code);
  }
  void onSettings(Envoy::Http::ReceivedSettings& settings) override { inner_.onSettings(settings); }
  void onMaxStreamsChanged(uint32_t num_streams) override {
    inner_.onMaxStreamsChanged(num_streams);
  }

  void notifyReporter() {
    if (notified_) {
      return;
    }

    if (auto* socket_manager = Bootstrap::ReverseConnection::ReverseTunnelAcceptorExtension::
            getThreadLocalSocketManager()) {
      socket_manager->onGoAway(fd_);
      notified_ = true;
    }
  }

  // Drive onGoAway into the pool's active client so the pool drains THIS connection (no new
  // streams, in-flight finish, close when idle). Needed because our drain GOAWAY is emitted
  // out-of-band, so the pool would not otherwise stop multiplexing onto it.
  void drainOwnPoolConnection() {
    if (auto* socket_manager = Bootstrap::ReverseConnection::ReverseTunnelAcceptorExtension::
            getThreadLocalSocketManager()) {
      socket_manager->markSocketDraining(fd_);
    }
    inner_.onGoAway(Envoy::Http::GoAwayErrorCode::NoError);
  }

  void onMetadata(Envoy::Http::MetadataMapPtr&& metadata_map_ptr) override {
    if (Bootstrap::ReverseConnection::ReverseConnectionUtility::eraseDrainMetadata(
            *metadata_map_ptr)) {
      ENVOY_LOG(debug, "reverse_tunnel upstream codec: received drain metadata");
      notifyReporter();
    }

    inner_.onMetadata(std::move(metadata_map_ptr));
  }

private:
  Envoy::Http::ConnectionCallbacks& inner_;
  int fd_;
  bool notified_{false};
};

/**
 * Decorates an upstream HTTP/2 client codec for reverse-tunnel clusters so it can emit a drain
 * METADATA frame and/or GOAWAY to the peer. Owns the DrainAwareClientCallbacks wrapper and forwards
 * every ClientConnection call to the wrapped codec.
 */
class DrainAwareClientConnection : public Envoy::Http::ClientConnection,
                                   public Logger::Loggable<Logger::Id::client> {
public:
  DrainAwareClientConnection(Envoy::Http::ClientConnectionPtr inner,
                             std::unique_ptr<DrainAwareClientCallbacks> callbacks,
                             const ReverseTunnelUpstreamCodecStats& stats,
                             Event::Dispatcher& dispatcher,
                             UpstreamCodecDrainRegistrySharedPtr registry,
                             absl::string_view cluster)
      : callbacks_(std::move(callbacks)), inner_(std::move(inner)), stats_(stats),
        dispatcher_(dispatcher), registry_(std::move(registry)), cluster_(cluster) {
    ENVOY_LOG(debug, "reverse_tunnel upstream codec: drain-aware client connection installed");
    if (registry_ != nullptr) {
      registry_->add(cluster_, *this);
    }
  }

  ~DrainAwareClientConnection() override {
    if (registry_ != nullptr) {
      registry_->remove(cluster_, *this);
    }
  }

  /**
   * Begin a graceful, two-phase drain from the upstream (client) side. Sends a stream-0 drain
   * METADATA frame immediately so the peer can redial a replacement tunnel (requires
   * `http2_protocol_options.allow_metadata` on the cluster), then sends a final GOAWAY and drains
   * the pool connection after `drain_time`. Idempotent.
   */
  void startGracefulDrain(std::chrono::milliseconds drain_time) {
    if (draining_) {
      return;
    }
    stats_.drain_started_.inc();
    draining_ = true;
    ENVOY_LOG(info, "reverse_tunnel upstream codec: starting graceful drain (final GOAWAY in {}ms)",
              drain_time.count());
    // Phase 2 (deferred): after the drain window, send the final GOAWAY and gracefully drain this
    // connection out of the pool. We deliberately do NOT force-close it: a hard close would abort
    // in-flight requests, whereas draining lets them finish while new requests move to the
    // replacement tunnel. The timer is armed BEFORE the Phase 1 send below because that send can
    // synchronously close the connection and destroy this object on a write error; since the timer
    // is owned by this, it is cancelled on destruction and no member is touched afterward.
    drain_timer_ = dispatcher_.createTimer([this]() {
      goAway();
      // NOTE: if this connection is already idle the pool closes it here, which may deferred-delete
      // this object; do not touch members afterwards.
      callbacks_->drainOwnPoolConnection();
    });
    drain_timer_->enableTimer(drain_time);

    sendDrainMetadata();
  }

  // Envoy::Http::ClientConnection
  Envoy::Http::RequestEncoder& newStream(Envoy::Http::ResponseDecoder& response_decoder) override {
    return inner_->newStream(response_decoder);
  }

  // Envoy::Http::Connection
  Envoy::Http::Status dispatch(Buffer::Instance& data) override { return inner_->dispatch(data); }
  void goAway() override {
    ENVOY_LOG(debug, "reverse_tunnel upstream codec: sending GOAWAY on drain");
    inner_->goAway();
  }
  Envoy::Http::Protocol protocol() override { return inner_->protocol(); }
  void shutdownNotice() override { inner_->shutdownNotice(); }
  bool wantsToWrite() override { return inner_->wantsToWrite(); }
  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override {
    inner_->onUnderlyingConnectionAboveWriteBufferHighWatermark();
  }
  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override {
    inner_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
  }
  void encodeMetadata(const Envoy::Http::MetadataMapVector& metadata_map_vector) override {
    inner_->encodeMetadata(metadata_map_vector);
  }

private:
  // Emits a stream-0 drain METADATA frame to the peer. Requires
  // `http2_protocol_options.allow_metadata` on the cluster; otherwise the underlying HTTP/2 codec
  // drops the frame and increments `http2.metadata_dropped`.
  void sendDrainMetadata() {
    inner_->encodeMetadata(
        Bootstrap::ReverseConnection::ReverseConnectionUtility::createDrainMetadata());
  }

  // Declared before inner_ so it outlives the codec, which holds a reference to it.
  const std::unique_ptr<DrainAwareClientCallbacks> callbacks_;
  const Envoy::Http::ClientConnectionPtr inner_;
  const ReverseTunnelUpstreamCodecStats& stats_;
  Event::Dispatcher& dispatcher_;
  const UpstreamCodecDrainRegistrySharedPtr registry_;
  const std::string cluster_;
  Event::TimerPtr drain_timer_;
  bool draining_{false};
};

} // namespace ReverseTunnel
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
