#include "source/common/quic/envoy_tls_server_handshaker.h"

#include "source/common/common/macros.h"
#include "source/common/quic/envoy_quic_server_session.h"

namespace Envoy {
namespace Quic {

EnvoyTlsServerHandshaker::EnvoyTlsServerHandshaker(
    quic::QuicSession* session, const quic::QuicCryptoServerConfig* crypto_config,
    Ssl::ServerContextSharedPtr pinned_ssl_ctx, bool disable_resumption, bool ticket_support_active)
    : TlsServerHandshaker(session, crypto_config), pinned_ssl_ctx_(std::move(pinned_ssl_ctx)) {
  const int ex_data_set = SSL_set_ex_data(ssl(), handshakerExDataIndex(), this);
  // In debug builds, catch unexpected ex_data setup failure. In release
  // builds the callbacks tolerate a missing handshaker and fail closed.
  ASSERT(ex_data_set == 1);

  // When Envoy ticket processing is active, guard against the pinned
  // context lacking keys even if the factory config snapshot said keys
  // exist. For connections where only key log is enabled, leave ticket
  // / resumption behavior untouched.
  if (disable_resumption ||
      (ticket_support_active &&
       (pinnedServerContext() == nullptr || !pinnedServerContext()->hasSessionTicketKeys()))) {
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
  auto* handshaker =
      static_cast<EnvoyTlsServerHandshaker*>(SSL_get_ex_data(ssl, handshakerExDataIndex()));
  ASSERT(handshaker == nullptr || dynamic_cast<EnvoyTlsServerHandshaker*>(handshaker) != nullptr);
  return handshaker;
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
  ASSERT(dynamic_cast<EnvoyQuicServerSession*>(handshaker->session()) != nullptr);
  const auto& info =
      static_cast<EnvoyQuicServerSession*>(handshaker->session())->connectionInfoProvider();
  handshaker->pinnedServerContext()->maybeWriteKeyLog(line, info.localAddress().get(),
                                                      info.remoteAddress().get());
}

} // namespace Quic
} // namespace Envoy
