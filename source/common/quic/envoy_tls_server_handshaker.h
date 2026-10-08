#pragma once

#include <openssl/ssl.h>

#include "source/common/common/assert.h"
#include "source/common/tls/server_context_impl.h"

#include "quiche/quic/core/tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

// Shared SSL_CTX callback bridge for QUIC connections, used when Envoy-managed session ticket
// processing, the QUIC key log feature, or downstream client certificate validation is enabled.
class EnvoyTlsServerHandshaker : public quic::TlsServerHandshaker {
public:
  EnvoyTlsServerHandshaker(quic::QuicSession* session,
                           const quic::QuicCryptoServerConfig* crypto_config,
                           Ssl::ServerContextSharedPtr pinned_ssl_ctx, bool disable_resumption);

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

  // quic::TlsServerHandshaker
  // Validates the downstream client certificate against the pinned server context, which is the
  // context of the filter chain already matched for this connection. This avoids re-selecting a
  // filter chain without the client's SNI, unlike a shared proof verifier.
  quic::QuicAsyncStatus
  VerifyCertChain(const std::vector<absl::string_view>& certs, std::string* error_details,
                  std::unique_ptr<quic::ProofVerifyDetails>* details, uint8_t* out_alert,
                  std::unique_ptr<quic::ProofVerifierCallback> callback) override;

private:
  // QuicServerTransportSocketFactory creates ServerContextImpl when sslCtx()
  // is available, so this downcast is safe for non-null contexts.
  Extensions::TransportSockets::Tls::ServerContextImpl* pinnedServerContext() const {
    return static_cast<Extensions::TransportSockets::Tls::ServerContextImpl*>(
        pinned_ssl_ctx_.get());
  }

  // Private to lock down the contract that the ex_data slot is written only
  // by this class's constructor.
  static int handshakerExDataIndex();
  static EnvoyTlsServerHandshaker* handshakerFromSsl(const SSL* ssl);

  Ssl::ServerContextSharedPtr pinned_ssl_ctx_;
};

} // namespace Quic
} // namespace Envoy
