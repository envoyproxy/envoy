#include "extensions/quic_listeners/quiche/envoy_quic_proof_verifier.h"

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

#include "quiche/quic/core/crypto/certificate_view.h"

namespace Envoy {
namespace Quic {

quic::QuicAsyncStatus EnvoyQuicProofVerifier::VerifyCertChain(
    const std::string& hostname, const uint16_t /*port*/, const std::vector<std::string>& certs,
    const std::string& /*ocsp_response*/, const std::string& /*cert_sct*/,
    const quic::ProofVerifyContext* /*context*/, std::string* error_details,
    std::unique_ptr<quic::ProofVerifyDetails>* /*details*/,
    std::unique_ptr<quic::ProofVerifierCallback> /*callback*/) {
  if (certs.empty()) {
    return quic::QUIC_FAILURE;
  }
  bssl::UniquePtr<STACK_OF(X509)> intermediates(sk_X509_new_null());
  bssl::UniquePtr<X509> leaf;
  for (size_t i = 0; i < certs.size(); i++) {
    bssl::UniquePtr<X509> cert = parseDERCertificate(certs[i], error_details);
    if (!cert) {
      return quic::QUIC_FAILURE;
    }
    if (i == 0) {
      leaf = std::move(cert);
    } else {
      sk_X509_push(intermediates.get(), cert.release());
    }
  }
  bool success =
      context_impl_.verifyCertChain(std::move(leaf), std::move(intermediates), *error_details);
  if (!success) {
    return quic::QUIC_FAILURE;
  }

  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(certs[0]);
  ASSERT(cert_view != nullptr);
  std::string wildcard = absl::StrCat("*", hostname.substr(hostname.find_first_of('.')));
  for (const absl::string_view config_san : cert_view->subject_alt_name_domains()) {
    if (config_san == hostname || config_san == wildcard) {
      return quic::QUIC_SUCCESS;
    }
  }
  *error_details = absl::StrCat("Leaf certificate doesn't match hostname: ", hostname);
  return quic::QUIC_FAILURE;
}

} // namespace Quic
} // namespace Envoy
