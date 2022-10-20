#include "source/common/quic/envoy_quic_proof_verifier.h"

#include <openssl/ssl.h>

#include <cstdint>
#include <memory>

#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/transport_sockets/tls/utility.h"

#include "quiche/quic/core/crypto/certificate_view.h"

namespace Envoy {
namespace Quic {

using ValidationResults = Envoy::Extensions::TransportSockets::Tls::ValidationResults;

namespace {

// Returns true if hostname matches one of the Subject Alt Names in cert_view. Returns false and
// sets error_details otherwise
bool verifyLeafCertMatchesHostname(quic::CertificateView& cert_view, const std::string& hostname,
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
                             const std::string& hostname, const std::string& leaf_cert)
      : dispatcher_(dispatcher), quic_callback_(std::move(quic_callback)), hostname_(hostname),
        leaf_cert_(leaf_cert) {}

  Event::Dispatcher& dispatcher() override { return dispatcher_; }

  void onCertValidationResult(bool succeeded, const std::string& error_details,
                              uint8_t /*tls_alert*/) override {
    if (!succeeded) {
      std::unique_ptr<quic::ProofVerifyDetails> details = std::make_unique<CertVerifyResult>(false);
      quic_callback_->Run(succeeded, error_details, &details);
      return;
    }
    std::string error;

    std::unique_ptr<quic::CertificateView> cert_view =
        quic::CertificateView::ParseSingleCertificate(leaf_cert_);
    succeeded = verifyLeafCertMatchesHostname(*cert_view, hostname_, &error);
    std::unique_ptr<quic::ProofVerifyDetails> details =
        std::make_unique<CertVerifyResult>(succeeded);
    quic_callback_->Run(succeeded, error, &details);
  }

private:
  Event::Dispatcher& dispatcher_;
  std::unique_ptr<quic::ProofVerifierCallback> quic_callback_;
  const std::string hostname_;
  // Leaf cert needs to be retained in case of asynchronous validation.
  std::string leaf_cert_;
};

} // namespace

quic::QuicAsyncStatus EnvoyQuicProofVerifier::VerifyCertChain(
    const std::string& hostname, const uint16_t port, const std::vector<std::string>& certs,
    const std::string& ocsp_response, const std::string& cert_sct,
    const quic::ProofVerifyContext* context, std::string* error_details,
    std::unique_ptr<quic::ProofVerifyDetails>* details, uint8_t* out_alert,
    std::unique_ptr<quic::ProofVerifierCallback> callback) {
  ASSERT(details != nullptr);
  ASSERT(!certs.empty());
  auto* verify_context = dynamic_cast<const EnvoyQuicProofVerifyContext*>(context);
  if (verify_context == nullptr) {
    IS_ENVOY_BUG("QUIC proof verify context was not setup correctly.");
    return quic::QUIC_FAILURE;
  }
  ENVOY_BUG(!verify_context->isServer(), "Client certificates are not supported in QUIC yet.");

  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.tls_async_cert_validation")) {
    if (doVerifyCertChain(hostname, port, certs, ocsp_response, cert_sct, context, error_details,
                          out_alert, std::move(callback))) {
      *details = std::make_unique<CertVerifyResult>(true);
      return quic::QUIC_SUCCESS;
    }
    *details = std::make_unique<CertVerifyResult>(false);
    return quic::QUIC_FAILURE;
  }

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  for (const auto& cert_str : certs) {
    bssl::UniquePtr<X509> cert = parseDERCertificate(cert_str, error_details);
    if (!cert || !bssl::PushToStack(cert_chain.get(), std::move(cert))) {
      return quic::QUIC_FAILURE;
    }
  }
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(certs[0]);
  ASSERT(cert_view != nullptr);
  int sign_alg = deduceSignatureAlgorithmFromPublicKey(cert_view->public_key(), error_details);
  if (sign_alg == 0) {
    return quic::QUIC_FAILURE;
  }

  auto envoy_callback = std::make_unique<QuicValidateResultCallback>(
      verify_context->dispatcher(), std::move(callback), hostname, certs[0]);
  ASSERT(dynamic_cast<Extensions::TransportSockets::Tls::ClientContextImpl*>(context_.get()) !=
         nullptr);
  // We down cast rather than add customVerifyCertChainForQuic to Envoy::Ssl::Context because
  // verifyCertChain uses a bunch of SSL-specific structs which we want to keep out of the interface
  // definition.
  ValidationResults result =
      static_cast<Extensions::TransportSockets::Tls::ClientContextImpl*>(context_.get())
          ->customVerifyCertChainForQuic(*cert_chain, std::move(envoy_callback),
                                         verify_context->isServer(),
                                         verify_context->transportSocketOptions(),
                                         verify_context->extraValidationContext(), hostname);
  if (result.status == ValidationResults::ValidationStatus::Pending) {
    return quic::QUIC_PENDING;
  }
  if (result.status == ValidationResults::ValidationStatus::Successful) {
    if (verifyLeafCertMatchesHostname(*cert_view, hostname, error_details)) {
      *details = std::make_unique<CertVerifyResult>(true);
      return quic::QUIC_SUCCESS;
    }
  } else {
    ASSERT(result.status == ValidationResults::ValidationStatus::Failed);
    if (result.error_details.has_value() && error_details) {
      *error_details = std::move(result.error_details.value());
    }
    if (result.tls_alert.has_value() && out_alert) {
      *out_alert = result.tls_alert.value();
    }
  }

  *details = std::make_unique<CertVerifyResult>(false);
  return quic::QUIC_FAILURE;
}

bool EnvoyQuicProofVerifier::doVerifyCertChain(
    const std::string& hostname, const uint16_t /*port*/, const std::vector<std::string>& certs,
    const std::string& /*ocsp_response*/, const std::string& /*cert_sct*/,
    const quic::ProofVerifyContext* /*context*/, std::string* error_details, uint8_t* /*out_alert*/,
    std::unique_ptr<quic::ProofVerifierCallback> /*callback*/) {
  bssl::UniquePtr<STACK_OF(X509)> intermediates(sk_X509_new_null());
  bssl::UniquePtr<X509> leaf;
  for (size_t i = 0; i < certs.size(); i++) {
    bssl::UniquePtr<X509> cert = parseDERCertificate(certs[i], error_details);
    if (!cert) {
      return false;
    }
    if (i == 0) {
      leaf = std::move(cert);
    } else {
      sk_X509_push(intermediates.get(), cert.release());
    }
  }
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(certs[0]);
  ASSERT(cert_view != nullptr);
  int sign_alg = deduceSignatureAlgorithmFromPublicKey(cert_view->public_key(), error_details);
  if (sign_alg == 0) {
    return false;
  }
  // We down cast rather than add verifyCertChain to Envoy::Ssl::Context because
  // verifyCertChain uses a bunch of SSL-specific structs which we want to keep
  // out of the interface definition.
  bool success = static_cast<Extensions::TransportSockets::Tls::ClientContextImpl*>(context_.get())
                     ->verifyCertChain(*leaf, *intermediates, *error_details);
  if (!success) {
    return false;
  }
  if (verifyLeafCertMatchesHostname(*cert_view, hostname, error_details)) {
    return true;
  }
  *error_details = absl::StrCat("Leaf certificate doesn't match hostname: ", hostname);
  return false;
}

} // namespace Quic
} // namespace Envoy
