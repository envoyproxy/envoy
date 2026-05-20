#include "source/common/quic/envoy_tls_server_handshaker.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Quic {

EnvoyTlsServerHandshaker::EnvoyTlsServerHandshaker(
    quic::QuicSession* session, const quic::QuicCryptoServerConfig* crypto_config,
    Ssl::ServerContextSharedPtr pinned_ssl_ctx, bool disable_resumption)
    : TlsServerHandshaker(session, crypto_config), pinned_ssl_ctx_(std::move(pinned_ssl_ctx)) {
  SSL_set_ex_data(ssl(), handshakerExDataIndex(), this);
  // Also check the pinned context for keys: the factory is shared across workers and
  // config_ may reflect an SDS update before ssl_ctx_ is swapped on the main thread.
  if (disable_resumption || !pinnedServerContext()->hasSessionTicketKeys()) {
    DisableResumption();
  }
}

int EnvoyTlsServerHandshaker::handshakerExDataIndex() {
  CONSTRUCT_ON_FIRST_USE(int, []() -> int {
    int index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    RELEASE_ASSERT(index >= 0, "Failed to allocate SSL ex_data index for handshaker");
    return index;
  }());
}

int EnvoyTlsServerHandshaker::ticketKeyCallback(SSL* ssl, uint8_t* key_name, uint8_t* iv,
                                                EVP_CIPHER_CTX* ctx, HMAC_CTX* hmac_ctx,
                                                int encrypt) {
  auto* handshaker =
      static_cast<EnvoyTlsServerHandshaker*>(SSL_get_ex_data(ssl, handshakerExDataIndex()));
  if (handshaker == nullptr || handshaker->pinnedServerContext() == nullptr) {
    // Null handshaker can occur if the runtime guard was toggled between
    // OnNewSslCtx (which installed this callback on the SSL_CTX) and
    // connection creation (which fell back to the vanilla TlsServerHandshaker).
    // Return 0 to disable ticket for this connection — graceful fallback.
    return 0;
  }
  return handshaker->pinnedServerContext()->sessionTicketProcess(ssl, key_name, iv, ctx, hmac_ctx,
                                                                 encrypt);
}

} // namespace Quic
} // namespace Envoy
