#include "source/common/quic/envoy_quic_proof_verifier.h"

#include <openssl/ssl.h>

#include <cstdint>
#include <memory>

#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/tls/client_context_impl.h"
#include "source/common/tls/utility.h"

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
                             const std::string& hostname, absl::string_view leaf_cert,
                             bool verify_hostname)
      : dispatcher_(dispatcher), quic_callback_(std::move(quic_callback)), hostname_(hostname),
        leaf_cert_(leaf_cert), verify_hostname_(verify_hostname) {}

  Event::Dispatcher& dispatcher() override { return dispatcher_; }

  void onCertValidationResult(bool succeeded, Ssl::ClientValidationStatus /*detailed_status*/,
                              const std::string& error_details, uint8_t /*tls_alert*/) override {
    std::string error;
    if (!succeeded) {
      error = error_details;
    } else if (verify_hostname_) {
      std::unique_ptr<quic::CertificateView> cert_view =
          quic::CertificateView::ParseSingleCertificate(leaf_cert_);
      succeeded = verifyLeafCertMatchesHostname(*cert_view, hostname_, &error);
    }
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
  // Whether the leaf certificate must carry a DNS SAN matching the hostname once the chain has
  // been validated.
  const bool verify_hostname_;
};

} // namespace

bool EnvoyQuicProofVerifier::hasExplicitIdentityCheck(
    const Envoy::Ssl::CertificateValidationContextConfig* validation_context) {
  if (validation_context == nullptr || validation_context->autoSniSanMatch()) {
    return false;
  }
  return !validation_context->subjectAltNameMatchers().empty() ||
         !validation_context->verifyCertificateHashList().empty() ||
         !validation_context->verifyCertificateSpkiList().empty() ||
         validation_context->customValidatorConfig().has_value();
}

quic::QuicAsyncStatus EnvoyQuicProofVerifier::VerifyCertChain(
    const std::string& hostname, const uint16_t /*port*/,
    const std::vector<absl::string_view>& certs, const std::string& /*ocsp_response*/,
    const std::string& /*cert_sct*/, const quic::ProofVerifyContext* context,
    std::string* error_details, std::unique_ptr<quic::ProofVerifyDetails>* details,
    uint8_t* out_alert, std::unique_ptr<quic::ProofVerifierCallback> callback) {
  auto* verify_context = dynamic_cast<const EnvoyQuicProofVerifyContext*>(context);
  if (verify_context == nullptr) {
    IS_ENVOY_BUG("QUIC proof verify context was not setup correctly.");
    return quic::QUIC_FAILURE;
  }
  ASSERT(details != nullptr);
  ASSERT(!certs.empty());
  // Server-side client certificate validation is handled by `EnvoyTlsServerHandshaker`.
  ASSERT(!verify_context->isServer());

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  for (const auto& cert_str : certs) {
    bssl::UniquePtr<X509> cert = parseDERCertificate(cert_str, error_details);
    if (!cert || !bssl::PushToStack(cert_chain.get(), std::move(cert))) {
      return quic::QUIC_FAILURE;
    }
  }
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(certs[0]);
  if (cert_view == nullptr) {
    *error_details = "unable to parse certificate";
    return quic::QUIC_FAILURE;
  }
  int sign_alg = deduceSignatureAlgorithmFromPublicKey(cert_view->public_key(), error_details);
  if (sign_alg == 0) {
    return quic::QUIC_FAILURE;
  }

  // Once the chain is validated the leaf certificate must carry a DNS SAN matching the hostname
  // (the SNI), unless untrusted certificates are accepted, or the operator configured an explicit
  // identity check (SAN matchers, certificate pins or a custom validator, either in the validation
  // context or as a per-connection SAN override) which replaces the SNI-derived check the same way
  // it does for the TLS client on TCP.
  const bool explicit_identity_check =
      explicit_identity_check_ ||
      (verify_context->transportSocketOptions() != nullptr &&
       !verify_context->transportSocketOptions()->verifySubjectAltNameListOverride().empty());
  const bool verify_hostname =
      !accept_untrusted_ &&
      !(explicit_identity_check &&
        Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.quic_hostname_check_deferred_to_explicit_san_match"));
  auto envoy_callback = std::make_unique<QuicValidateResultCallback>(
      verify_context->dispatcher(), std::move(callback), hostname, std::string(certs[0]),
      verify_hostname);
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
    if (!verify_hostname || verifyLeafCertMatchesHostname(*cert_view, hostname, error_details)) {
      // Hand the verifier-built chain (not the peer-sent list) to the details so the connection
      // info can report the validated issuer.
      *details = std::make_unique<CertVerifyResult>(true, std::move(result.validated_chain));
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

} // namespace Quic
} // namespace Envoy
