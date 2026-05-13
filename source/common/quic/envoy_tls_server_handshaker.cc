#include "source/common/quic/envoy_tls_server_handshaker.h"

#include "source/common/common/macros.h"
#include "source/common/quic/envoy_quic_server_session.h"

namespace Envoy {
namespace Quic {

EnvoyTlsServerHandshaker::EnvoyTlsServerHandshaker(
    quic::QuicSession* session, const quic::QuicCryptoServerConfig* crypto_config,
    Ssl::ServerContextSharedPtr pinned_ssl_ctx, bool disable_resumption)
    : TlsServerHandshaker(session, crypto_config), pinned_ssl_ctx_(std::move(pinned_ssl_ctx)) {
  RELEASE_ASSERT(SSL_set_ex_data(ssl(), handshakerExDataIndex(), this) == 1,
                 "Failed to set SSL ex_data for QUIC handshaker");
  // Refuse resumption for this connection if the caller already decided to
  // disable it or the pinned context lost its ticket keys (the latter can
  // happen when SDS leaves the factory config ahead of ssl_ctx_ on the worker).
  // DisableResumption sets SSL_OP_NO_TICKET on the SSL, which is what gates
  // the QUIC/TLS 1.3 ticket paths in BoringSSL.
  if (disable_resumption || pinnedServerContext() == nullptr ||
      !pinnedServerContext()->hasSessionTicketKeys()) {
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
