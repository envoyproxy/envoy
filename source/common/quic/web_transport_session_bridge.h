#pragma once

#include <memory>
#include <vector>

#include "envoy/http/codec.h"

#include "source/common/common/logger.h"

#include "quiche/web_transport/web_transport.h"

namespace Envoy {
namespace Quic {

/**
 * Bridges a single WebTransport stream between downstream and upstream.
 * Reads data from one side and writes to the other, handling backpressure.
 */
// The two streams are named "local" and "peer" rather than "downstream"/"upstream" because a
// single stream bridge is symmetric: at the session level it pairs a stream accepted on one
// session with a stream opened on the other, and which of those is the proxy's downstream vs
// upstream depends on which session created the bridge (see WebTransportSessionBridge).
// The visitors below are owned by the QUIC streams (via Stream::SetVisitor). The bridge is
// reference-counted and co-owned by its two visitors (each holds a shared_ptr to it), so it lives
// as long as at least one of its streams and is freed once both are gone. During connection
// teardown one side's stream/adapter can be destroyed before the other's; when a visitor is
// destroyed (its stream is gone) it nulls the corresponding stream pointer on the bridge, so the
// surviving visitor never dereferences a freed peer.
class WebTransportStreamBridge : protected Logger::Loggable<Logger::Id::quic_stream>,
                                 public std::enable_shared_from_this<WebTransportStreamBridge> {
public:
  WebTransportStreamBridge(webtransport::Stream* local, webtransport::Stream* peer);
  ~WebTransportStreamBridge() = default;

  /**
   * Visitor installed on the local stream. Forwards data local->peer.
   */
  class LocalVisitor : public webtransport::StreamVisitor {
  public:
    explicit LocalVisitor(std::shared_ptr<WebTransportStreamBridge> bridge)
        : bridge_(std::move(bridge)) {}
    // The local stream (and its adapter) is being destroyed; drop the now-dangling pointer so the
    // peer's visitor never dereferences it.
    ~LocalVisitor() override;

    void OnCanRead() override;
    void OnCanWrite() override;
    void OnResetStreamReceived(webtransport::StreamErrorCode error) override;
    void OnStopSendingReceived(webtransport::StreamErrorCode error) override;
    void OnWriteSideInDataRecvdState() override {}

  private:
    std::shared_ptr<WebTransportStreamBridge> bridge_;
  };

  /**
   * Visitor installed on the peer stream. Forwards data peer->local.
   */
  class PeerVisitor : public webtransport::StreamVisitor {
  public:
    explicit PeerVisitor(std::shared_ptr<WebTransportStreamBridge> bridge)
        : bridge_(std::move(bridge)) {}
    ~PeerVisitor() override;

    void OnCanRead() override;
    void OnCanWrite() override;
    void OnResetStreamReceived(webtransport::StreamErrorCode error) override;
    void OnStopSendingReceived(webtransport::StreamErrorCode error) override;
    void OnWriteSideInDataRecvdState() override {}

  private:
    std::shared_ptr<WebTransportStreamBridge> bridge_;
  };

  std::unique_ptr<LocalVisitor> makeLocalVisitor() {
    return std::make_unique<LocalVisitor>(shared_from_this());
  }
  std::unique_ptr<PeerVisitor> makePeerVisitor() {
    return std::make_unique<PeerVisitor>(shared_from_this());
  }

  // Must be called once after the visitors are installed on both streams. The
  // WebTransportStreamAdapter of QUICHE does not replay OnCanRead for data that arrived before
  // SetVisitor(), so any bytes already buffered on either stream would otherwise be stranded. This
  // pumps both directions once to drain that pre-buffered data; subsequent data is driven by
  // OnCanRead.
  void start();

private:
  // Transfer data from src stream to dst stream. Returns false if write was blocked.
  bool transferData(webtransport::Stream* src, webtransport::Stream* dst);

  // Raw pointers into the QUIC streams' WebTransport adapters. Each is nulled by the corresponding
  // visitor's destructor when that stream goes away, so callbacks must null-check before use.
  webtransport::Stream* local_;
  webtransport::Stream* peer_;
  bool local_read_blocked_{false};
  bool peer_read_blocked_{false};
};
using WebTransportStreamBridgeSharedPtr = std::shared_ptr<WebTransportStreamBridge>;

/**
 * Shared state object stored in FilterState to pass WebTransportHttp3 session pointers
 * between upstream and downstream codecs.
 */
class WebTransportSessionState {
public:
  // Sets (or, with nullptr, clears) the WebTransport session pointer for each side. A side is
  // cleared by the WebTransportSessionBridge installed on that session when it (and thus the
  // session) is destroyed, so the surviving side never bridges to a freed session.
  //
  // The session is stored as the webtransport::Session interface rather than the concrete
  // quic::WebTransportHttp3: every forwarding call the bridge makes (accept/open streams, send
  // datagrams) is part of that interface, and decoupling from the concrete type both keeps the
  // dependency minimal and lets the forwarding logic be unit-tested with a mock session.
  // installBridge() obtains these pointers from
  // Http::WebTransportSession::rawWebTransportSession().
  void setUpstreamSession(webtransport::Session* session) { upstream_ = session; }
  void setDownstreamSession(webtransport::Session* session) { downstream_ = session; }
  webtransport::Session* upstreamSession() const { return upstream_; }
  webtransport::Session* downstreamSession() const { return downstream_; }

  // Whether each side's WebTransport session is still alive (its pointer not yet cleared on
  // teardown). Checked before forwarding so neither side bridges to a dangling session pointer.
  bool upstreamAlive() const { return upstream_ != nullptr; }
  bool downstreamAlive() const { return downstream_ != nullptr; }

private:
  webtransport::Session* upstream_{nullptr};
  webtransport::Session* downstream_{nullptr};
};
using WebTransportSessionStateSharedPtr = std::shared_ptr<WebTransportSessionState>;

/**
 * Session-level bridge between a downstream and upstream WebTransport session.
 * Implements webtransport::SessionVisitor to receive incoming streams and datagrams
 * on one side and forward them to the other.
 */
class WebTransportSessionBridge : public webtransport::SessionVisitor,
                                  protected Logger::Loggable<Logger::Id::quic_stream> {
public:
  /**
   * @param local_session the session this visitor is installed on.
   * @param peer_session the session on the other side of the proxy.
   */
  WebTransportSessionBridge(WebTransportSessionStateSharedPtr session_state, bool is_downstream);
  // This visitor is owned by its WebTransportHttp3 session (via SetVisitor), so it is destroyed
  // when that session is torn down. Clear this side's session pointer in the shared state so the
  // peer's bridge (still alive on the other session) stops forwarding to the now-freed session.
  ~WebTransportSessionBridge() override;

  // webtransport::SessionVisitor
  void OnSessionReady() override;
  void OnSessionClosed(webtransport::SessionErrorCode error_code,
                       const std::string& error_message) override;
  void OnIncomingBidirectionalStreamAvailable() override;
  void OnIncomingUnidirectionalStreamAvailable() override;
  void OnDatagramReceived(absl::string_view datagram) override;
  void OnCanCreateNewOutgoingBidirectionalStream() override {}
  void OnCanCreateNewOutgoingUnidirectionalStream() override {}

  /**
   * Creates the shared WebTransportSessionState and installs a WebTransportSessionBridge on each
   * session, co-owning that state. Takes the Http::WebTransportSession interface (implemented by
   * the QUIC streams) rather than the concrete quic::WebTransportHttp3 so this can be unit-tested
   * with a mock: setWebTransportVisitor() wraps the concrete SetVisitor(), and
   * rawWebTransportSession() supplies the webtransport::Session interface stored in the state for
   * forwarding. A no-op if either session has no underlying WebTransport session. The state is an
   * internal detail shared between the two bridges, so it is created here rather than by the
   * caller.
   */
  static void installBridge(Http::WebTransportSession& downstream_session,
                            Http::WebTransportSession& upstream_session);

  webtransport::Session* localSession() const {
    return is_downstream_ ? session_state_->downstreamSession() : session_state_->upstreamSession();
  }
  webtransport::Session* peerSession() const {
    return is_downstream_ ? session_state_->upstreamSession() : session_state_->downstreamSession();
  }

private:
  void bridgeIncomingStream(webtransport::Stream* local_stream, webtransport::Stream* peer_stream);

  // Clears this bridge's own side of the shared state, so the peer bridge stops forwarding to this
  // session. Called both when the session closes (OnSessionClosed) and when it is destroyed (the
  // destructor), since the former can fire while the session object is still alive.
  void clearLocalSession() {
    if (is_downstream_) {
      session_state_->setDownstreamSession(nullptr);
    } else {
      session_state_->setUpstreamSession(nullptr);
    }
  }

  WebTransportSessionStateSharedPtr session_state_;
  const bool is_downstream_{};
  bool closed_{false};
};

} // namespace Quic
} // namespace Envoy
