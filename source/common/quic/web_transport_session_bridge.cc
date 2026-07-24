#include "source/common/quic/web_transport_session_bridge.h"

#include <array>

#include "quiche/common/simple_buffer_allocator.h"

namespace Envoy {
namespace Quic {

// --- WebTransportStreamBridge ---

WebTransportStreamBridge::WebTransportStreamBridge(webtransport::Stream* local,
                                                   webtransport::Stream* peer)
    : local_(local), peer_(peer) {}

bool WebTransportStreamBridge::transferData(webtransport::Stream* src, webtransport::Stream* dst) {
  // Move all currently-available data from src to dst. Returns false if dst's write side is
  // blocked, in which case nothing is read out of src this turn (so no data is lost) and the
  // transfer resumes when dst's OnCanWrite fires.
  std::array<char, 16384> buffer;
  while (true) {
    // Check that dst can accept a write *before* reading from src. WebTransport has no way to put
    // bytes back into the stream, so reading before a failed write would drop them.
    if (!dst->CanWrite()) {
      return false;
    }

    webtransport::Stream::ReadResult result = src->Read(absl::MakeSpan(buffer));
    if (result.bytes_read == 0 && !result.fin) {
      // Nothing more to read right now.
      return true;
    }
    ENVOY_LOG(trace, "WebTransport stream bridge read {} bytes (fin={}) from stream {}",
              result.bytes_read, result.fin, src->GetStreamId());

    webtransport::StreamWriteOptions options;
    options.set_send_fin(result.fin);
    absl::Status write_result;
    if (result.bytes_read > 0) {
      quiche::QuicheMemSlice slice(
          quiche::QuicheBuffer::Copy(quiche::SimpleBufferAllocator::Get(),
                                     absl::string_view(buffer.data(), result.bytes_read)));
      absl::Span<quiche::QuicheMemSlice> span(&slice, 1);
      write_result = dst->Writev(span, options);
    } else {
      // FIN with no data.
      write_result = dst->Writev(absl::Span<quiche::QuicheMemSlice>(), options);
    }
    if (!write_result.ok()) {
      // CanWrite() returned true, so this is not flow-control backpressure; treat as terminal for
      // this direction rather than retrying (which would spin).
      ENVOY_LOG(debug, "WebTransport stream bridge write failed: {}", write_result.message());
      return true;
    }
    ENVOY_LOG(trace, "WebTransport stream bridge wrote {} bytes (fin={}) to stream {}",
              result.bytes_read, result.fin, dst->GetStreamId());
    if (result.fin) {
      // Write side is done; nothing further to forward in this direction.
      return true;
    }
  }
}

void WebTransportStreamBridge::start() {
  if (local_ == nullptr || peer_ == nullptr) {
    return;
  }
  // Data may already be buffered on either stream from before SetVisitor() was called; QUICHE does
  // not replay OnCanRead for it, so pump both directions once now.
  if (!transferData(local_, peer_)) {
    local_read_blocked_ = true;
  }
  if (!transferData(peer_, local_)) {
    peer_read_blocked_ = true;
  }
}

// LocalVisitor: reads from local, writes to peer.
WebTransportStreamBridge::LocalVisitor::~LocalVisitor() { bridge_->local_ = nullptr; }

void WebTransportStreamBridge::LocalVisitor::OnCanRead() {
  if (bridge_->local_ == nullptr || bridge_->peer_ == nullptr) {
    return;
  }
  if (!bridge_->transferData(bridge_->local_, bridge_->peer_)) {
    // Peer write side is full; pause reading local until peer becomes writable (resumed by
    // PeerVisitor::OnCanWrite).
    bridge_->local_read_blocked_ = true;
  }
}

void WebTransportStreamBridge::LocalVisitor::OnCanWrite() {
  if (bridge_->local_ == nullptr || bridge_->peer_ == nullptr) {
    return;
  }
  // Local can accept writes again. The peer->local direction may have been paused because the
  // local write side was full; resume it.
  if (bridge_->peer_read_blocked_) {
    bridge_->peer_read_blocked_ = false;
    if (!bridge_->transferData(bridge_->peer_, bridge_->local_)) {
      bridge_->peer_read_blocked_ = true;
    }
  }
}

void WebTransportStreamBridge::LocalVisitor::OnResetStreamReceived(
    webtransport::StreamErrorCode error) {
  ENVOY_LOG(debug, "WebTransport local stream reset received, code={}", error);
  if (bridge_->peer_ != nullptr) {
    bridge_->peer_->ResetWithUserCode(error);
  }
}

void WebTransportStreamBridge::LocalVisitor::OnStopSendingReceived(
    webtransport::StreamErrorCode error) {
  ENVOY_LOG(debug, "WebTransport local stop sending received, code={}", error);
  if (bridge_->peer_ != nullptr) {
    bridge_->peer_->SendStopSending(error);
  }
}

// PeerVisitor: reads from peer, writes to local.
WebTransportStreamBridge::PeerVisitor::~PeerVisitor() { bridge_->peer_ = nullptr; }

void WebTransportStreamBridge::PeerVisitor::OnCanRead() {
  if (bridge_->local_ == nullptr || bridge_->peer_ == nullptr) {
    return;
  }
  if (!bridge_->transferData(bridge_->peer_, bridge_->local_)) {
    // Local write side is full; pause reading peer until local becomes writable (resumed by
    // LocalVisitor::OnCanWrite).
    bridge_->peer_read_blocked_ = true;
  }
}

void WebTransportStreamBridge::PeerVisitor::OnCanWrite() {
  if (bridge_->local_ == nullptr || bridge_->peer_ == nullptr) {
    return;
  }
  // Peer can accept writes again. The local->peer direction may have been paused because the peer
  // write side was full; resume it.
  if (bridge_->local_read_blocked_) {
    bridge_->local_read_blocked_ = false;
    if (!bridge_->transferData(bridge_->local_, bridge_->peer_)) {
      bridge_->local_read_blocked_ = true;
    }
  }
}

void WebTransportStreamBridge::PeerVisitor::OnResetStreamReceived(
    webtransport::StreamErrorCode error) {
  ENVOY_LOG(debug, "WebTransport peer stream reset received, code={}", error);
  if (bridge_->local_ != nullptr) {
    bridge_->local_->ResetWithUserCode(error);
  }
}

void WebTransportStreamBridge::PeerVisitor::OnStopSendingReceived(
    webtransport::StreamErrorCode error) {
  ENVOY_LOG(debug, "WebTransport peer stop sending received, code={}", error);
  if (bridge_->local_ != nullptr) {
    bridge_->local_->SendStopSending(error);
  }
}

// --- WebTransportSessionBridge ---

WebTransportSessionBridge::WebTransportSessionBridge(
    WebTransportSessionStateSharedPtr session_state, bool is_downstream)
    : session_state_(std::move(session_state)), is_downstream_(is_downstream) {}

WebTransportSessionBridge::~WebTransportSessionBridge() {
  // This side's session is being destroyed; clear its pointer so the peer bridge stops forwarding.
  clearLocalSession();
}

void WebTransportSessionBridge::OnSessionReady() {
  ENVOY_LOG(debug, "WebTransport session bridge: session ready");
}

void WebTransportSessionBridge::OnSessionClosed(webtransport::SessionErrorCode error_code,
                                                const std::string& error_message) {
  if (closed_) {
    return;
  }
  closed_ = true;
  ENVOY_LOG(debug, "WebTransport session closed, code={}, message={}", error_code, error_message);
  // The session can be closed (e.g. a CLOSE_WEBTRANSPORT_SESSION capsule or CONNECT-stream FIN was
  // received) while its object is still alive, i.e. before this visitor is destroyed. Clear this
  // side now so the peer bridge stops forwarding into the closed session rather than waiting for
  // the destructor.
  clearLocalSession();
}

void WebTransportSessionBridge::bridgeIncomingStream(webtransport::Stream* local_stream,
                                                     webtransport::Stream* peer_stream) {
  if (!local_stream || !peer_stream) {
    ENVOY_LOG(warn, "WebTransport bridge: failed to create stream pair");
    if (local_stream) {
      local_stream->ResetWithUserCode(0);
    }
    return;
  }
  auto bridge = std::make_shared<WebTransportStreamBridge>(local_stream, peer_stream);
  // The two visitors co-own the bridge (each holds a shared_ptr) and are owned by their streams via
  // SetVisitor, so the bridge lives as long as either stream and is freed once both close. No
  // separate ownership by the session bridge is needed.
  local_stream->SetVisitor(bridge->makeLocalVisitor());
  peer_stream->SetVisitor(bridge->makePeerVisitor());
  // Drain any data that arrived before the visitors were installed (QUICHE does not replay
  // OnCanRead for it).
  bridge->start();
}

void WebTransportSessionBridge::OnIncomingBidirectionalStreamAvailable() {
  if (!session_state_->downstreamAlive() || !session_state_->upstreamAlive() || closed_) {
    ENVOY_LOG(debug, "WebTransport session bridge: ignoring incoming stream because peer is dead");
    return;
  }

  while (true) {
    webtransport::Stream* local_stream = localSession()->AcceptIncomingBidirectionalStream();
    if (!local_stream) {
      break;
    }
    webtransport::Stream* peer_stream = peerSession()->OpenOutgoingBidirectionalStream();
    bridgeIncomingStream(local_stream, peer_stream);
  }
}

void WebTransportSessionBridge::OnIncomingUnidirectionalStreamAvailable() {
  if (!session_state_->downstreamAlive() || !session_state_->upstreamAlive() || closed_) {
    ENVOY_LOG(debug, "WebTransport session bridge: ignoring incoming stream because peer is dead");
    return;
  }

  while (true) {
    webtransport::Stream* local_stream = localSession()->AcceptIncomingUnidirectionalStream();
    if (!local_stream) {
      break;
    }
    webtransport::Stream* peer_stream = peerSession()->OpenOutgoingUnidirectionalStream();
    bridgeIncomingStream(local_stream, peer_stream);
  }
}

void WebTransportSessionBridge::OnDatagramReceived(absl::string_view datagram) {
  if (!session_state_->downstreamAlive() || !session_state_->upstreamAlive() || closed_) {
    ENVOY_LOG(debug,
              "WebTransport session bridge: ignoring incoming datagram because peer is dead");
    return;
  }

  peerSession()->SendOrQueueDatagram(datagram);
}

// static
void WebTransportSessionBridge::installBridge(Http::WebTransportSession& downstream_session,
                                              Http::WebTransportSession& upstream_session) {
  // Both underlying QUICHE sessions must be present to bridge them (defensive: the caller only
  // invokes this once both are known, see EnvoyQuicClientStream::mayInitializeWebTransportState).
  webtransport::Session* downstream_raw = downstream_session.rawWebTransportSession();
  webtransport::Session* upstream_raw = upstream_session.rawWebTransportSession();
  if (downstream_raw == nullptr || upstream_raw == nullptr) {
    return;
  }
  // The two bridges created below co-own this state, so it outlives installBridge. Store the
  // sessions as the webtransport::Session interface so each bridge can forward to the other.
  auto session_state = std::make_shared<WebTransportSessionState>();
  session_state->setDownstreamSession(downstream_raw);
  session_state->setUpstreamSession(upstream_raw);

  auto ds_bridge = std::make_unique<WebTransportSessionBridge>(session_state, true);
  auto us_bridge = std::make_unique<WebTransportSessionBridge>(session_state, false);
  // Transfer ownership to the sessions via setWebTransportVisitor (which wraps the concrete
  // WebTransportHttp3::SetVisitor; the interface decouples installBridge from the concrete type).
  downstream_session.setWebTransportVisitor(std::move(ds_bridge));
  upstream_session.setWebTransportVisitor(std::move(us_bridge));
}

} // namespace Quic
} // namespace Envoy
