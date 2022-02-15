#include "source/common/quic/envoy_quic_proof_verifier.h"

#include "source/common/quic/envoy_quic_utils.h"
#include "source/extensions/transport_sockets/tls/utility.h"

#include "quiche/quic/core/crypto/certificate_view.h"

namespace Envoy {
namespace Quic {

quic::QuicAsyncStatus EnvoyQuicProofVerifier::VerifyCertChain(
    const std::string& hostname, const uint16_t port, const std::vector<std::string>& certs,
    const std::string& ocsp_response, const std::string& cert_sct,
    const quic::ProofVerifyContext* context, std::string* error_details,
    std::unique_ptr<quic::ProofVerifyDetails>* details, uint8_t* out_alert,
    std::unique_ptr<quic::ProofVerifierCallback> callback) {
  ASSERT(details != nullptr);
  if (doVerifyCertChain(hostname, port, certs, ocsp_response, cert_sct, context, error_details,
                        out_alert, std::move(callback))) {
    *details = std::make_unique<CertVerifyResult>(true);
    return quic::QUIC_SUCCESS;
  }
  *details = std::make_unique<CertVerifyResult>(false);
  return quic::QUIC_FAILURE;
}

bool EnvoyQuicProofVerifier::doVerifyCertChain(
    const std::string& hostname, const uint16_t /*port*/, const std::vector<std::string>& certs,
    const std::string& /*ocsp_response*/, const std::string& /*cert_sct*/,
    const quic::ProofVerifyContext* /*context*/, std::string* error_details, uint8_t* /*out_alert*/,
    std::unique_ptr<quic::ProofVerifierCallback> /*callback*/) {
  ASSERT(!certs.empty());
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

  for (const absl::string_view& config_san : cert_view->subject_alt_name_domains()) {
    if (Extensions::TransportSockets::Tls::Utility::dnsNameMatch(hostname, config_san)) {
      return true;
    }
  }
  *error_details = absl::StrCat("Leaf certificate doesn't match hostname: ", hostname);
  return false;
}

} // namespace Quic
} // namespace Envoy
