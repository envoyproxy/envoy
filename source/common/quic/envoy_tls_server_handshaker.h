#pragma once

#include <openssl/ssl.h>

#include "source/common/common/assert.h"
#include "source/common/tls/server_context_impl.h"

#include "quiche/quic/core/tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

// Minimal TlsServerHandshaker subclass that pins a ServerContextImpl at
// connection creation time. Ensures session ticket keys remain valid for
// the connection's lifetime, matching TCP TLS behavior.
//
// Also owns the per-connection session ticket callback plumbing:
// self-registers in SSL ex_data so ticketKeyCallback() can retrieve
// the pinned context from an SSL* in a C callback.
class EnvoyTlsServerHandshaker : public quic::TlsServerHandshaker {
public:
  EnvoyTlsServerHandshaker(quic::QuicSession* session,
                           const quic::QuicCryptoServerConfig* crypto_config,
                           Ssl::ServerContextSharedPtr pinned_ssl_ctx, bool disable_resumption);

  // Session ticket key callback installed on QUICHE's SSL_CTX.
  // Retrieves EnvoyTlsServerHandshaker from SSL ex_data and delegates to
  // the pinned ServerContextImpl::sessionTicketProcess().
  static int ticketKeyCallback(SSL* ssl, uint8_t* key_name, uint8_t* iv, EVP_CIPHER_CTX* ctx,
                               HMAC_CTX* hmac_ctx, int encrypt);

  // SSL ex_data index for storing the handshaker pointer per-connection.
  static int handshakerExDataIndex();

private:
  // QuicServerTransportSocketFactory always creates ServerContextImpl,
  // so this downcast is safe for all QUIC connections.
  Extensions::TransportSockets::Tls::ServerContextImpl* pinnedServerContext() const {
    return static_cast<Extensions::TransportSockets::Tls::ServerContextImpl*>(
        pinned_ssl_ctx_.get());
  }

  Ssl::ServerContextSharedPtr pinned_ssl_ctx_;
};

} // namespace Quic
} // namespace Envoy
