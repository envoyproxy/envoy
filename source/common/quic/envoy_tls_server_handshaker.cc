#include "source/common/quic/envoy_tls_server_handshaker.h"

#include "source/common/common/macros.h"
#include "source/common/quic/envoy_quic_server_session.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Quic {

EnvoyTlsServerHandshaker::EnvoyTlsServerHandshaker(
    quic::QuicSession* session, const quic::QuicCryptoServerConfig* crypto_config,
    Ssl::ServerContextSharedPtr pinned_ssl_ctx, QuicSessionTicketConfig ticket_config)
    : TlsServerHandshaker(session, crypto_config), pinned_ssl_ctx_(std::move(pinned_ssl_ctx)),
      process_envoy_session_tickets_(ticket_config.has_keys &&
                                     !ticket_config.handles_session_resumption) {
  RELEASE_ASSERT(SSL_set_ex_data(ssl(), handshakerExDataIndex(), this) == 1,
                 "Failed to set SSL ex_data for QUIC handshaker");
  // The runtime flag gates whether Envoy's ticket key callback is installed on
  // the shared SSL_CTX (see EnvoyQuicProofSource::OnNewSslCtx). When off,
  // QUICHE/BoringSSL handle tickets via their existing path and the handshaker
  // must not call DisableResumption() — doing so would set SSL_OP_NO_TICKET
  // and break 0-RTT for the default flag-off config.
  const bool envoy_ticket_callback_enabled =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.quic_session_ticket_support");
  bool disable_resumption = false;
  if (envoy_ticket_callback_enabled) {
    if (ticket_config.disable_stateless_resumption) {
      // Operator opted out of stateless resumption entirely.
      disable_resumption = true;
    } else if (process_envoy_session_tickets_) {
      // Envoy owns ticket processing for this listener. Guard against the
      // SDS race where the factory config rotates ahead of ssl_ctx_ on the
      // worker, leaving the pinned context without keys.
      disable_resumption =
          pinnedServerContext() == nullptr || !pinnedServerContext()->hasSessionTicketKeys();
    }
    // flag on + no Envoy keys, or a TLS handshaker that owns resumption:
    // leave SSL_OP_NO_TICKET unchanged. The shared ticket key callback is
    // still installed on the SSL_CTX, but for this connection it will see
    // process_envoy_session_tickets_ == false and return 0, declining
    // Envoy-managed ticket processing for this connection.
  }
  if (disable_resumption) {
    const bool disabled = DisableResumption();
    ASSERT(disabled);
  }
}

int EnvoyTlsServerHandshaker::handshakerExDataIndex() {
  CONSTRUCT_ON_FIRST_USE(int, []() -> int {
    int index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    RELEASE_ASSERT(index >= 0, "Failed to allocate SSL ex_data index for handshaker");
    return index;
  }());
}

EnvoyTlsServerHandshaker* EnvoyTlsServerHandshaker::handshakerFromSsl(const SSL* ssl) {
  // Null is valid for the vanilla QUICHE fallback path.
  return static_cast<EnvoyTlsServerHandshaker*>(SSL_get_ex_data(ssl, handshakerExDataIndex()));
}

int EnvoyTlsServerHandshaker::ticketKeyCallback(SSL* ssl, uint8_t* key_name, uint8_t* iv,
                                                EVP_CIPHER_CTX* ctx, HMAC_CTX* hmac_ctx,
                                                int encrypt) {
  auto* handshaker = handshakerFromSsl(ssl);
  if (handshaker == nullptr || handshaker->pinnedServerContext() == nullptr) {
    return 0;
  }
  // Per-connection mirror of TCP's gating (TCP avoids installing the callback
  // when handles_session_resumption is set or keys are missing; QUIC shares
  // one SSL_CTX, so we gate inside the callback instead). When the per-
  // connection config says Envoy doesn't own ticket processing, decline.
  if (!handshaker->process_envoy_session_tickets_) {
    return 0;
  }
  // Final guard before entering ServerContextImpl::sessionTicketProcess(),
  // whose encrypt path requires non-empty session ticket keys. This is
  // normally covered by ctor policy / DisableResumption(), but keeps the
  // shared callback safe.
  if (!handshaker->pinnedServerContext()->hasSessionTicketKeys()) {
    return 0;
  }
  return handshaker->pinnedServerContext()->sessionTicketProcess(ssl, key_name, iv, ctx, hmac_ctx,
                                                                 encrypt);
}

void EnvoyTlsServerHandshaker::keylogCallback(const SSL* ssl, const char* line) {
  auto* handshaker = handshakerFromSsl(ssl);
  if (handshaker == nullptr || handshaker->pinnedServerContext() == nullptr) {
    return;
  }
  // EnvoyQuicServerSession is-a Network::Connection, so reuse the cached
  // envoy address objects from its connection info provider rather than
  // re-converting QUICHE addresses on every key log line.
  ASSERT(dynamic_cast<EnvoyQuicServerSession*>(handshaker->session()) != nullptr);
  const auto& info =
      static_cast<EnvoyQuicServerSession*>(handshaker->session())->connectionInfoProvider();
  handshaker->pinnedServerContext()->maybeWriteKeyLog(line, info.localAddress().get(),
                                                      info.remoteAddress().get());
}

} // namespace Quic
} // namespace Envoy
