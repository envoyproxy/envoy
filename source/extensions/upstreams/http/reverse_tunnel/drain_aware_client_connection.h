#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/codec.h"

#include "source/common/common/logger.h"
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

constexpr absl::string_view metadata_val{"true"};

/**
 * Wraps the codec connection callbacks so the upstream (client) side observes a peer GOAWAY: the
 * event is forwarded to the reverse-tunnel socket manager for reporter notification, then passed
 * through to the pool's normal drain handling.
 */
class DrainAwareClientCallbacks : public Envoy::Http::ConnectionCallbacks,
                                  public Logger::Loggable<Logger::Id::client> {
public:
  // `metadata_key` is the connection-metadata key the upstream watches for a drain signal from the
  // peer. When null, every received connection-metadata map is forwarded to the inner callbacks
  // unchanged (no reporter notification). Defaults to null so non-reverse-tunnel callers that do
  // not care about drain metadata can construct the wrapper without it.
  DrainAwareClientCallbacks(Envoy::Http::ConnectionCallbacks& inner, int fd,
                            std::shared_ptr<std::string> metadata_key = nullptr)
      : inner_(inner), fd_(fd), metadata_key_(std::move(metadata_key)) {}

  // Envoy::Http::ConnectionCallbacks
  void onGoAway(Envoy::Http::GoAwayErrorCode error_code) override {
    ENVOY_LOG(debug, "reverse_tunnel upstream codec: observed peer GOAWAY (error_code={})",
              static_cast<int>(error_code));
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
  void drainOwnPoolConnection() { inner_.onGoAway(Envoy::Http::GoAwayErrorCode::NoError); }

  void onMetadata(Envoy::Http::MetadataMapPtr&& metadata_map_ptr) override {
    ENVOY_LOG(debug, "reverse_tunnel upstream codec: received connection metadata ({} entries)",
              metadata_map_ptr->size());
    if (!metadata_key_) {
      return inner_.onMetadata(std::move(metadata_map_ptr));
    }

    auto it = metadata_map_ptr->find(*metadata_key_);
    if (it != metadata_map_ptr->end() && it->second == metadata_val) {
      ENVOY_LOG(info,
                "reverse_tunnel upstream codec: drain metadata matched key='{}'; "
                "notifying reporter",
                *metadata_key_);
      notifyReporter();
      metadata_map_ptr->erase(it);
    }

    inner_.onMetadata(std::move(metadata_map_ptr));
  }

private:
  Envoy::Http::ConnectionCallbacks& inner_;
  int fd_;
  bool notified_{false};
  std::shared_ptr<std::string> metadata_key_;
};

/**
 * Decorates an upstream HTTP/2 client codec for reverse-tunnel clusters so it can emit a drain
 * GOAWAY to the peer. Owns the DrainAwareClientCallbacks wrapper and forwards every
 * ClientConnection call to the wrapped codec.
 */
class DrainAwareClientConnection : public Envoy::Http::ClientConnection,
                                   public Logger::Loggable<Logger::Id::client> {
public:
  // `h2_codec` is a typed, non-owning view of `inner` (the same object) used to emit the graceful
  // first GOAWAY; nullptr falls back to shutdownNotice().
  DrainAwareClientConnection(Envoy::Http::ClientConnectionPtr inner,
                             std::unique_ptr<DrainAwareClientCallbacks> callbacks,
                             const ReverseTunnelUpstreamCodecStats& stats,
                             Event::Dispatcher& dispatcher,
                             UpstreamCodecDrainRegistrySharedPtr registry,
                             absl::string_view cluster, std::shared_ptr<std::string> metadata_key)
      : callbacks_(std::move(callbacks)), inner_(std::move(inner)), stats_(stats),
        dispatcher_(dispatcher), registry_(std::move(registry)), cluster_(cluster),
        metadata_key_(metadata_key) {
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
    // Phase 2 (deferred): after the drain window, send the final GOAWAY and gracefully drain this
    // connection out of the pool. We deliberately do NOT force-close it: a hard close would abort
    // in-flight requests, whereas draining lets them finish while new requests move to the
    // replacement tunnel. The timer is armed BEFORE the Phase 1 send below because that send can
    // synchronously close the connection and destroy this object on a write error; since the timer
    // is owned by this, it is cancelled on destruction and no member is touched afterward.
    drain_timer_ = dispatcher_.createTimer([this]() {
      ENVOY_LOG(debug,
                "reverse_tunnel upstream codec: drain timer fired; final GOAWAY + pool drain");
      stats_.goaway_sent_.inc();
      inner_->goAway();
      // NOTE: if this connection is already idle the pool closes it here, which may deferred-delete
      // this object; do not touch members afterwards.
      callbacks_->drainOwnPoolConnection();
    });
    drain_timer_->enableTimer(drain_time);
    sendMetadata();
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
  void sendMetadata() {
    if (!metadata_key_) {
      return;
    }

    ENVOY_LOG(info, "reverse_tunnel upstream codec: sending drain metadata (key='{}')",
              *metadata_key_);
    Envoy::Http::MetadataMap metadata_map;
    metadata_map.emplace(*metadata_key_, metadata_val);
    Envoy::Http::MetadataMapPtr metadata_map_ptr =
        std::make_unique<Envoy::Http::MetadataMap>(std::move(metadata_map));
    Envoy::Http::MetadataMapVector metadata_map_vector;
    metadata_map_vector.push_back(std::move(metadata_map_ptr));

    inner_->encodeMetadata(std::move(metadata_map_vector));
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
  std::shared_ptr<std::string> metadata_key_;
};

} // namespace ReverseTunnel
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
