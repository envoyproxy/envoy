#pragma once

#include <openssl/ssl.h>

#include "envoy/network/connection.h"

#include "source/common/common/assert.h"
#include "source/common/tls/server_context_impl.h"

#include "quiche/quic/core/tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

// TlsServerHandshaker subclass for QUIC session ticket handling.
//
// The session ticket key callback is installed on the shared QUICHE ssl
// context, so every connection reaches the same callback regardless of which
// filter chain served it. To find the right session ticket keys at callback
// time, each connection pins a shared pointer to its ServerContextImpl in
// ssl ex data at creation time. The pinned pointer keeps the context alive
// for the connection even after an SDS update rotates the factory's active
// context, and it matches TCP TLS behavior where each connection is bound
// to the ServerContextImpl that was current at connection creation.
class EnvoyTlsServerHandshaker : public quic::TlsServerHandshaker {
public:
  // `envoy_connection` is the EnvoyQuicServerSession passed in via its
  // Network::Connection facet by the caller; the handshaker holds it as a
  // raw pointer to avoid a header/dep cycle with envoy_quic_server_session.
  EnvoyTlsServerHandshaker(quic::QuicSession* session,
                           const quic::QuicCryptoServerConfig* crypto_config,
                           Ssl::ServerContextSharedPtr pinned_ssl_ctx,
                           Network::Connection* envoy_connection, bool disable_resumption);

  // Session ticket key callback installed on the QUICHE ssl context.
  // Retrieves the handshaker from ssl ex_data and delegates to the pinned
  // ServerContextImpl::sessionTicketProcess().
  static int ticketKeyCallback(SSL* ssl, uint8_t* key_name, uint8_t* iv, EVP_CIPHER_CTX* ctx,
                               HMAC_CTX* hmac_ctx, int encrypt);

  // Key log callback installed on the QUICHE ssl context. Retrieves the
  // handshaker from ssl ex_data and writes an NSS Key Log line via the
  // pinned ServerContextImpl, applying the same local/remote IP-list
  // filtering as TCP TLS key log. Connection addresses are read from the
  // QUIC session at callback time.
  static void keylogCallback(const SSL* ssl, const char* line);

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
  Network::Connection* envoy_connection_;
};

} // namespace Quic
} // namespace Envoy
