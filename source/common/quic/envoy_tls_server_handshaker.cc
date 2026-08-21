#include "source/common/quic/envoy_tls_server_handshaker.h"

#include "source/common/common/macros.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_server_session.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_filter_manager_connection_impl.h"

namespace Envoy {
namespace Quic {

using ValidationResults = Envoy::Extensions::TransportSockets::Tls::ValidationResults;

EnvoyTlsServerHandshaker::EnvoyTlsServerHandshaker(
    quic::QuicSession* session, const quic::QuicCryptoServerConfig* crypto_config,
    Ssl::ServerContextSharedPtr pinned_ssl_ctx, bool disable_resumption,
    Envoy::Event::Dispatcher& dispatcher)
    : TlsServerHandshaker(session, crypto_config), pinned_ssl_ctx_(std::move(pinned_ssl_ctx)),
      dispatcher_(dispatcher),
      envoy_connection_(dynamic_cast<QuicFilterManagerConnectionImpl*>(session)) {
  SSL_set_ex_data(ssl(), handshakerExDataIndex(), this);
  if (disable_resumption) {
    DisableResumption();
  }
}

EnvoyTlsServerHandshaker::~EnvoyTlsServerHandshaker() {
  if (pending_validation_callback_ != nullptr) {
    pending_validation_callback_->cancel();
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
      std::make_unique<ServerQuicValidateResultCallback>(*this, dispatcher_, std::move(callback));
  // Retain a non-owning pointer so a pending validation can be cancelled if this handshaker is
  // destroyed first. The callback object is owned by the cert validator.
  ServerQuicValidateResultCallback* envoy_callback_ptr = envoy_callback.get();
  ValidationResults result = pinnedServerContext()->customVerifyCertChainForQuic(
      *cert_chain, std::move(envoy_callback), /*is_server=*/true,
      /*transport_socket_options=*/nullptr, /*validation_context=*/{}, /*host_name=*/"");
  if (result.status == ValidationResults::ValidationStatus::Pending) {
    pending_validation_callback_ = envoy_callback_ptr;
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

void EnvoyTlsServerHandshaker::onAsyncCertValidationDone(
    bool succeeded, const std::string& error_details,
    std::unique_ptr<quic::ProofVerifierCallback> quic_callback) {
  pending_validation_callback_ = nullptr;
  // The QUICHE proof verify completion (ProofVerifierCallbackImpl::Run) resumes the handshake with
  // a plain AdvanceHandshake(), which asserts on the server side that a packet flusher is
  // attached. Mirror quic::TlsServerHandshaker::AdvanceHandshakeFromCallback by attaching a
  // flusher around the resumption and notifying the delegate afterwards.
  quic::QuicConnection::ScopedPacketFlusher flusher(session()->connection());
  std::unique_ptr<quic::ProofVerifyDetails> details = std::make_unique<CertVerifyResult>(succeeded);
  quic_callback->Run(succeeded, error_details, &details);
  if (!is_connection_closed()) {
    handshaker_delegate()->OnHandshakeCallbackDone();
  }
}

} // namespace Quic
} // namespace Envoy
