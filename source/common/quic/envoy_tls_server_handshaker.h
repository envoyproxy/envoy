#pragma once

#include <openssl/ssl.h>

#include "envoy/event/dispatcher.h"

#include "source/common/common/assert.h"
#include "source/common/tls/server_context_impl.h"

#include "quiche/quic/core/tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

class QuicFilterManagerConnectionImpl;

// TlsServerHandshaker subclass for QUIC session ticket handling and client
// certificate validation.
//
// The session ticket key callback is installed on the shared QUICHE ssl
// context, so every connection reaches the same callback regardless of which
// filter chain served it. To find the right session ticket keys at callback
// time, each connection pins a shared pointer to its ServerContextImpl in
// ssl ex data at creation time. The pinned pointer keeps the context alive
// for the connection even after an SDS update rotates the factory's active
// context, and it matches TCP TLS behavior where each connection is bound
// to the ServerContextImpl that was current at connection creation.
//
// Client certificates presented during the handshake (requested when the filter chain's
// downstream TLS context sets require_client_certificate, see
// EnvoyQuicServerSession::GetSSLConfig()) are validated in VerifyCertChain against the pinned
// context's validation context, with support for asynchronous cert validators.
class EnvoyTlsServerHandshaker : public quic::TlsServerHandshaker,
                                 protected Logger::Loggable<Logger::Id::quic> {
public:
  EnvoyTlsServerHandshaker(quic::QuicSession* session,
                           const quic::QuicCryptoServerConfig* crypto_config,
                           Ssl::ServerContextSharedPtr pinned_ssl_ctx, bool disable_resumption,
                           Envoy::Event::Dispatcher& dispatcher);

  // Session ticket key callback installed on the QUICHE ssl context.
  // Retrieves the handshaker from ssl ex_data and delegates to the pinned
  // ServerContextImpl::sessionTicketProcess().
  static int ticketKeyCallback(SSL* ssl, uint8_t* key_name, uint8_t* iv, EVP_CIPHER_CTX* ctx,
                               HMAC_CTX* hmac_ctx, int encrypt);

  // SSL ex_data index for storing the handshaker pointer per-connection.
  static int handshakerExDataIndex();

  // quic::TlsHandshaker (via quic::TlsServerHandshaker)
  // Validates the client certificate chain against the pinned server context. Only invoked by
  // QUICHE when the client presented a certificate.
  quic::QuicAsyncStatus VerifyCertChain(const std::vector<absl::string_view>& certs,
                                        std::string* error_details,
                                        std::unique_ptr<quic::ProofVerifyDetails>* details,
                                        uint8_t* out_alert,
                                        std::unique_ptr<quic::ProofVerifierCallback>
                                            callback) override;
  // Invoked on both synchronous and asynchronous validation completion; marks the connection's
  // SSL info as validated on success.
  void OnProofVerifyDetailsAvailable(const quic::ProofVerifyDetails& verify_details) override;

private:
  // QuicServerTransportSocketFactory always creates ServerContextImpl,
  // so this downcast is safe for all QUIC connections.
  Extensions::TransportSockets::Tls::ServerContextImpl* pinnedServerContext() const {
    return static_cast<Extensions::TransportSockets::Tls::ServerContextImpl*>(
        pinned_ssl_ctx_.get());
  }

  Ssl::ServerContextSharedPtr pinned_ssl_ctx_;
  Envoy::Event::Dispatcher& dispatcher_;
  // The session as an Envoy connection, for notifying cert validation results. Null in tests
  // that use a bare QuicSession.
  QuicFilterManagerConnectionImpl* envoy_connection_{nullptr};
};

} // namespace Quic
} // namespace Envoy
