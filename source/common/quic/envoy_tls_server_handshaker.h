#pragma once

#include <openssl/ssl.h>

#include "source/common/common/assert.h"
#include "source/common/quic/quic_session_ticket_config.h"
#include "source/common/tls/server_context_impl.h"

#include "quiche/quic/core/tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

// TlsServerHandshaker subclass that bridges shared QUICHE SSL_CTX callbacks
// to per-connection Envoy state, including Envoy-managed stateless session
// ticket processing and key logging.
//
// The shared callbacks are installed on the shared QUICHE ssl context, so
// every connection reaches the same callback regardless of which filter
// chain served it. To find the right per-connection state at callback time,
// each connection stores its EnvoyTlsServerHandshaker in ssl ex_data; the
// handshaker pins a shared pointer to its ServerContextImpl, keeping the
// context alive for the connection even after an SDS update rotates the
// factory's active context. This matches TCP TLS behavior where each
// connection is bound to the ServerContextImpl that was current at
// connection creation.
class EnvoyTlsServerHandshaker : public quic::TlsServerHandshaker {
public:
  EnvoyTlsServerHandshaker(quic::QuicSession* session,
                           const quic::QuicCryptoServerConfig* crypto_config,
                           Ssl::ServerContextSharedPtr pinned_ssl_ctx,
                           QuicSessionTicketConfig ticket_config);

  // Session ticket key callback installed on the QUICHE ssl context.
  // Retrieves the handshaker from ssl ex_data and delegates to the pinned
  // ServerContextImpl::sessionTicketProcess(). BoringSSL invokes this when
  // SSL_OP_NO_TICKET is not set on the SSL. The constructor sets
  // SSL_OP_NO_TICKET only for explicit resumption disable or when
  // Envoy-owned ticket processing was selected but the pinned context
  // cannot serve keys. Other non-Envoy-ticket cases decline inside the
  // callback.
  static int ticketKeyCallback(SSL* ssl, uint8_t* key_name, uint8_t* iv, EVP_CIPHER_CTX* ctx,
                               HMAC_CTX* hmac_ctx, int encrypt);

  // Key log callback installed on the QUICHE ssl context. Retrieves the
  // handshaker from ssl ex_data and writes an NSS Key Log line via the
  // pinned ServerContextImpl, applying the same local/remote IP-list
  // filtering as TCP TLS key log. Connection addresses are read from the
  // QUIC session at callback time.
  static void keylogCallback(const SSL* ssl, const char* line);

private:
  // QuicServerTransportSocketFactory creates ServerContextImpl when sslCtx()
  // is available, so this downcast is safe for non-null contexts.
  Extensions::TransportSockets::Tls::ServerContextImpl* pinnedServerContext() const {
    return static_cast<Extensions::TransportSockets::Tls::ServerContextImpl*>(
        pinned_ssl_ctx_.get());
  }

  // Private to lock down the contract that the ex_data slot is written only
  // by this class's constructor and stores either nullptr or this.
  static int handshakerExDataIndex();
  static EnvoyTlsServerHandshaker* handshakerFromSsl(const SSL* ssl);

  Ssl::ServerContextSharedPtr pinned_ssl_ctx_;
  // Snapshot of "Envoy owns ticket processing for this connection" computed
  // at ctor time: has_keys && !handles_session_resumption. ticketKeyCallback
  // reads it to mirror the TCP gating (TCP avoids installing the callback at
  // all when handles_session_resumption is set; QUIC shares one SSL_CTX
  // across filter chains, so the gate has to live per-connection).
  const bool process_envoy_session_tickets_;
};

} // namespace Quic
} // namespace Envoy
