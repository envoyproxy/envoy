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
#include "source/extensions/upstreams/http/reverse_tunnel/drain_aware_http2_client_connection.h"
#include "source/extensions/upstreams/http/reverse_tunnel/drain_registry.h"
#include "source/extensions/upstreams/http/reverse_tunnel/reverse_tunnel_codec_stats.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace ReverseTunnel {

/**
 * Wraps the codec connection callbacks so the upstream (client) side can observe a peer GOAWAY on
 * a reverse tunnel. When the downstream (HTTP server) emits GOAWAY as it drains or rotates a
 * tunnel, the upstream client codec calls back into these callbacks with onGoAway(),
 * which we log + count before forwarding to the real callbacks so the pool's normal drain handling
 * is preserved. (Reporter wiring is intentionally deferred per design; log + stat for now.)
 */
class DrainAwareClientCallbacks : public Envoy::Http::ConnectionCallbacks,
                                  public Logger::Loggable<Logger::Id::client> {
public:
  DrainAwareClientCallbacks(Envoy::Http::ConnectionCallbacks& inner,
                            const ReverseTunnelUpstreamCodecStats& stats)
      : inner_(inner), stats_(stats) {}

  // Envoy::Http::ConnectionCallbacks
  void onGoAway(Envoy::Http::GoAwayErrorCode error_code) override {
    ENVOY_LOG(debug, "reverse_tunnel upstream codec: observed peer GOAWAY (error_code={})",
              static_cast<int>(error_code));
    stats_.goaway_received_.inc();
    inner_.onGoAway(error_code);
  }
  void onSettings(Envoy::Http::ReceivedSettings& settings) override { inner_.onSettings(settings); }
  void onMaxStreamsChanged(uint32_t num_streams) override {
    inner_.onMaxStreamsChanged(num_streams);
  }

  // Ask the connection pool to gracefully drain THIS connection by driving the codec's onGoAway
  // into the pool's active client (MultiplexedActiveClientBase::onGoAway): the pool stops assigning
  // new streams to this connection, lets in-flight streams finish, and closes it once idle. After
  // that, new egress requests are served by the downstream's replacement tunnel.
  //
  // We invoke this ourselves because the drain GOAWAY is emitted out-of-band (the peer did not send
  // it), so the pool would otherwise never learn this connection is draining and would keep
  // multiplexing new requests onto it. This is the local-drain path, so it is NOT counted as a
  // received peer GOAWAY (goaway_received_ is left untouched).
  void drainOwnPoolConnection() { inner_.onGoAway(Envoy::Http::GoAwayErrorCode::NoError); }

private:
  Envoy::Http::ConnectionCallbacks& inner_;
  const ReverseTunnelUpstreamCodecStats& stats_;
};

/**
 * Decorates an upstream HTTP/2 client codec for reverse-tunnel clusters, where the TCP
 * client/server roles are reversed relative to the HTTP roles. It owns a DrainAwareClientCallbacks
 * wrapper (installed at codec construction so received GOAWAYs are observed) and forwards every
 * ClientConnection call to the wrapped codec. goAway()/shutdownNotice() are the hooks for emitting
 * a drain GOAWAY to the peer.
 */
class DrainAwareClientConnection : public Envoy::Http::ClientConnection,
                                   public Logger::Loggable<Logger::Id::client> {
public:
  // `h2_codec` is a non-owning, typed view of `inner` when the inner codec is our HTTP/2 subclass
  // (it is the same object as `inner`). It is used to emit the graceful first GOAWAY; nullptr
  // disables the two-phase send and falls back to shutdownNotice().
  DrainAwareClientConnection(Envoy::Http::ClientConnectionPtr inner,
                             std::unique_ptr<DrainAwareClientCallbacks> callbacks,
                             const ReverseTunnelUpstreamCodecStats& stats,
                             Event::Dispatcher& dispatcher,
                             UpstreamCodecDrainRegistrySharedPtr registry,
                             absl::string_view cluster,
                             DrainAwareHttp2ClientConnection* h2_codec = nullptr)
      : callbacks_(std::move(callbacks)), inner_(std::move(inner)), stats_(stats),
        dispatcher_(dispatcher), registry_(std::move(registry)), cluster_(cluster),
        h2_codec_(h2_codec) {
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
   * Begin a graceful, two-phase drain from the upstream (client) side. Sends a shutdown notice
   * immediately (HTTP/2 GOAWAY with max stream id, so existing/in-flight streams keep working) and
   * a real GOAWAY after `drain_time`. Idempotent.
   */
  void startGracefulDrain(std::chrono::milliseconds drain_time) {
    if (draining_) {
      return;
    }
    draining_ = true;
    ENVOY_LOG(info,
              "reverse_tunnel upstream codec: starting graceful drain (first GOAWAY now, final "
              "GOAWAY in {}ms)",
              drain_time.count());
    // Phase 1: graceful GOAWAY with max stream id. shutdownNotice() is a no-op on a client codec
    // (server-only), so use the subclass's direct SubmitGoAway when available.
    if (h2_codec_ != nullptr) {
      stats_.goaway_sent_.inc();
      h2_codec_->sendGracefulGoAway();
    } else {
      inner_->shutdownNotice();
    }
    // Phase 2: after the drain window, send the final GOAWAY (real last-stream-id) and gracefully
    // DRAIN this connection out of the upstream pool -- we deliberately do NOT force-close it.
    // Draining makes the pool stop assigning new streams here (new egress requests rebuild onto the
    // downstream's replacement tunnel, or fast-fail to a retryable 503 if none is cached) while
    // in-flight requests finish on this connection; the pool closes it once idle. A hard close
    // would abort in-flight requests, which is exactly what we want to avoid.
    drain_timer_ = dispatcher_.createTimer([this]() {
      ENVOY_LOG(debug, "reverse_tunnel upstream codec: drain timer fired; sending final GOAWAY and "
                       "draining the pool connection (in-flight requests finish; new requests use "
                       "the replacement tunnel)");
      stats_.goaway_sent_.inc();
      inner_->goAway();
      // NOTE: if this connection is already idle the pool closes it here, which may deferred-delete
      // this object; do not touch members afterwards.
      callbacks_->drainOwnPoolConnection();
    });
    drain_timer_->enableTimer(drain_time);
  }

  // Envoy::Http::ClientConnection
  Envoy::Http::RequestEncoder& newStream(Envoy::Http::ResponseDecoder& response_decoder) override {
    return inner_->newStream(response_decoder);
  }

  // Envoy::Http::Connection
  Envoy::Http::Status dispatch(Buffer::Instance& data) override { return inner_->dispatch(data); }
  void goAway() override {
    ENVOY_LOG(debug, "reverse_tunnel upstream codec: sending GOAWAY on drain");
    stats_.goaway_sent_.inc();
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

private:
  // Declared before inner_ so it outlives the codec, which holds a reference to it.
  const std::unique_ptr<DrainAwareClientCallbacks> callbacks_;
  const Envoy::Http::ClientConnectionPtr inner_;
  const ReverseTunnelUpstreamCodecStats& stats_;
  Event::Dispatcher& dispatcher_;
  const UpstreamCodecDrainRegistrySharedPtr registry_;
  const std::string cluster_;
  // Non-owning typed view of inner_ when it is our HTTP/2 subclass; nullptr otherwise.
  DrainAwareHttp2ClientConnection* const h2_codec_;
  Event::TimerPtr drain_timer_;
  bool draining_{false};
};

} // namespace ReverseTunnel
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
