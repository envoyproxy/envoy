#include "source/common/quic/envoy_tls_server_handshaker.h"

#include "source/common/common/macros.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_filter_manager_connection_impl.h"

namespace Envoy {
namespace Quic {

using ValidationResults = Envoy::Extensions::TransportSockets::Tls::ValidationResults;

namespace {

// Bridges the completion of an asynchronous Envoy cert validation to the QUICHE handshake.
// Unlike the client-side counterpart in envoy_quic_proof_verifier.cc, no hostname matching is
// performed: client certificates are not tied to a hostname.
class ServerQuicValidateResultCallback : public Ssl::ValidateResultCallback {
public:
  ServerQuicValidateResultCallback(Event::Dispatcher& dispatcher,
                                   std::unique_ptr<quic::ProofVerifierCallback>&& quic_callback)
      : dispatcher_(dispatcher), quic_callback_(std::move(quic_callback)) {}

  Event::Dispatcher& dispatcher() override { return dispatcher_; }

  void onCertValidationResult(bool succeeded, Ssl::ClientValidationStatus /*detailed_status*/,
                              const std::string& error_details, uint8_t /*tls_alert*/) override {
    std::unique_ptr<quic::ProofVerifyDetails> details =
        std::make_unique<CertVerifyResult>(succeeded);
    quic_callback_->Run(succeeded, succeeded ? "" : error_details, &details);
  }

private:
  Event::Dispatcher& dispatcher_;
  std::unique_ptr<quic::ProofVerifierCallback> quic_callback_;
};

} // namespace

EnvoyTlsServerHandshaker::EnvoyTlsServerHandshaker(
    quic::QuicSession* session, const quic::QuicCryptoServerConfig* crypto_config,
    Ssl::ServerContextSharedPtr pinned_ssl_ctx, bool disable_resumption,
    Envoy::Event::Dispatcher& dispatcher)
    : TlsServerHandshaker(session, crypto_config), pinned_ssl_ctx_(std::move(pinned_ssl_ctx)),
      dispatcher_(dispatcher),
      envoy_connection_(dynamic_cast<QuicFilterManagerConnectionImpl*>(session)) {
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

quic::QuicAsyncStatus EnvoyTlsServerHandshaker::VerifyCertChain(
    const std::vector<absl::string_view>& certs, std::string* error_details,
    std::unique_ptr<quic::ProofVerifyDetails>* details, uint8_t* out_alert,
    std::unique_ptr<quic::ProofVerifierCallback> callback) {
  ASSERT(details != nullptr);
  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  for (const absl::string_view& cert_str : certs) {
    bssl::UniquePtr<X509> cert = parseDERCertificate(cert_str, error_details);
    if (!cert || !bssl::PushToStack(cert_chain.get(), std::move(cert))) {
      *details = std::make_unique<CertVerifyResult>(false);
      return quic::QUIC_FAILURE;
    }
  }

  auto envoy_callback =
      std::make_unique<ServerQuicValidateResultCallback>(dispatcher_, std::move(callback));
  ValidationResults result = pinnedServerContext()->customVerifyCertChainForQuic(
      *cert_chain, std::move(envoy_callback), /*is_server=*/true,
      /*transport_socket_options=*/nullptr, /*validation_context=*/{}, /*host_name=*/"");
  if (result.status == ValidationResults::ValidationStatus::Pending) {
    return quic::QUIC_PENDING;
  }
  if (result.status == ValidationResults::ValidationStatus::Successful) {
    *details = std::make_unique<CertVerifyResult>(true);
    return quic::QUIC_SUCCESS;
  }

  ASSERT(result.status == ValidationResults::ValidationStatus::Failed);
  ENVOY_LOG(debug, "Client certificate validation failed: {}",
            result.error_details.value_or("unknown error"));
  if (result.error_details.has_value() && error_details != nullptr) {
    *error_details = std::move(result.error_details.value());
  }
  if (result.tls_alert.has_value() && out_alert != nullptr) {
    *out_alert = result.tls_alert.value();
  }
  *details = std::make_unique<CertVerifyResult>(false);
  return quic::QUIC_FAILURE;
}

void EnvoyTlsServerHandshaker::OnProofVerifyDetailsAvailable(
    const quic::ProofVerifyDetails& verify_details) {
  const auto* result = dynamic_cast<const CertVerifyResult*>(&verify_details);
  if (result != nullptr && result->isValid() && envoy_connection_ != nullptr) {
    envoy_connection_->onCertValidated();
  }
}

} // namespace Quic
} // namespace Envoy
