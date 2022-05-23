#include "source/common/quic/envoy_quic_proof_verifier.h"

#include <openssl/safestack.h>
#include <openssl/ssl.h>

#include <cstdint>
#include <memory>

#include "source/common/quic/envoy_quic_utils.h"
#include "source/extensions/transport_sockets/tls/utility.h"

#include "quiche/quic/core/crypto/certificate_view.h"

namespace Envoy {
namespace Quic {

namespace {

bool verifyLeafCertMatchesHostName(quic::CertificateView& cert_view, const std::string& hostname,
                                   std::string* error_details) {
  for (const absl::string_view& config_san : cert_view.subject_alt_name_domains()) {
    if (Extensions::TransportSockets::Tls::Utility::dnsNameMatch(hostname, config_san)) {
      return true;
    }
  }
  *error_details = absl::StrCat("Leaf certificate doesn't match hostname: ", hostname);
  return false;
}

class QuicValidateResultCallback : public Ssl::ValidateResultCallback {
public:
  QuicValidateResultCallback(Event::Dispatcher& dispatcher,
                             std::unique_ptr<quic::ProofVerifierCallback>&& quic_callback,
                             const std::string& hostname)
      : dispatcher_(dispatcher), quic_callback_(std::move(quic_callback)), hostname_(hostname) {}

  Event::Dispatcher& dispatcher() override { return dispatcher_; }

  void onCertValidationResult(bool succeeded, const std::string& error_details,
                              uint8_t /*out_alert*/) override {
    if (!succeeded) {
      std::unique_ptr<quic::ProofVerifyDetails> details = std::make_unique<CertVerifyResult>(false);
      quic_callback_->Run(succeeded, error_details, &details);
      return;
    }
    std::string error;

    std::unique_ptr<quic::CertificateView> cert_view =
        quic::CertificateView::ParseSingleCertificate(leaf_cert_);
    succeeded = verifyLeafCertMatchesHostName(*cert_view, hostname_, &error);
    std::unique_ptr<quic::ProofVerifyDetails> details =
        std::make_unique<CertVerifyResult>(succeeded);
    quic_callback_->Run(succeeded, error, &details);
  }

  void storeLeafCert(const std::string& leaf_cert) { leaf_cert_ = leaf_cert; }

private:
  Event::Dispatcher& dispatcher_;
  std::unique_ptr<quic::ProofVerifierCallback> quic_callback_;
  std::string leaf_cert_;
  const std::string hostname_;
};

} // namespace

absl::string_view EnvoyQuicProofVerifyContextImpl::getEchNameOverrride() const {
  const char* name = nullptr;
  size_t name_len = 0;
  SSL_get0_ech_name_override(ssl_info_.ssl(), &name, &name_len);
  return absl::string_view(name, name_len);
}

quic::QuicAsyncStatus EnvoyQuicProofVerifier::VerifyCertChain(
    const std::string& hostname, const uint16_t /*port*/, const std::vector<std::string>& certs,
    const std::string& /*ocsp_response*/, const std::string& /*cert_sct*/,
    const quic::ProofVerifyContext* context, std::string* error_details,
    std::unique_ptr<quic::ProofVerifyDetails>* details, uint8_t* out_alert,
    std::unique_ptr<quic::ProofVerifierCallback> callback) {
  ASSERT(details != nullptr);
  ASSERT(!certs.empty());
  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  for (size_t i = 0; i < certs.size(); i++) {
    bssl::UniquePtr<X509> cert = parseDERCertificate(certs[i], error_details);
    if (!cert) {
      return quic::QUIC_FAILURE;
    }
    sk_X509_push(cert_chain.get(), cert.release());
  }
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(certs[0]);
  ASSERT(cert_view != nullptr);
  int sign_alg = deduceSignatureAlgorithmFromPublicKey(cert_view->public_key(), error_details);
  if (sign_alg == 0) {
    return quic::QUIC_FAILURE;
  }

  auto* verify_context = static_cast<const EnvoyQuicProofVerifyContext*>(context);
  ASSERT(verify_context);
  auto* envoy_callback = new QuicValidateResultCallback(
      const_cast<EnvoyQuicProofVerifyContext*>(verify_context)->dispatcher(), std::move(callback),
      hostname);
  // We down cast rather than add verifyCertChain to Envoy::Ssl::Context because
  // verifyCertChain uses a bunch of SSL-specific structs which we want to keep
  // out of the interface definition.
  Ssl::ValidateResult result =
      static_cast<Extensions::TransportSockets::Tls::ClientContextImpl*>(context_.get())
          ->customVerifyCertChainForQuic(
              *cert_chain, std::unique_ptr<QuicValidateResultCallback>(envoy_callback),
              verify_context->isServer(), /*transport_socket_options=*/nullptr,
              verify_context->getEchNameOverrride(), error_details, out_alert);
  if (result == Ssl::ValidateResult::Pending) {
    // Transfer the ownership of cert_view to callback while asynchronously verifying the cert
    // chain.
    envoy_callback->storeLeafCert(certs[0]);
    return quic::QUIC_PENDING;
  }
  if (result == Ssl::ValidateResult::Successful) {
    if (verifyLeafCertMatchesHostName(*cert_view, hostname, error_details)) {
      *details = std::make_unique<CertVerifyResult>(true);
      return quic::QUIC_SUCCESS;
    }
  }
  *details = std::make_unique<CertVerifyResult>(false);
  return quic::QUIC_FAILURE;
}

} // namespace Quic
} // namespace Envoy
