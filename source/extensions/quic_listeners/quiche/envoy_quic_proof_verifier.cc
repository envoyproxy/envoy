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

  bssl::UniquePtr<X509_STORE_CTX> ctx(X509_STORE_CTX_new());
  // It doesn't matter which SSL context is used, because they share the same
  // cert validation config.
  X509_STORE* store = SSL_CTX_get_cert_store(context_impl_.chooseSslContexts());
  if (!X509_STORE_CTX_init(ctx.get(), store, leaf.get(), intermediates.get())) {
    *error_details = "Failed to verify certificate chain: X509_STORE_CTX_init";
    return quic::QUIC_FAILURE;
  }

  int res = context_impl_.doVerifyCertChain(ctx.get(), nullptr, std::move(leaf), nullptr);
  if (res <= 0) {
    const int n = X509_STORE_CTX_get_error(ctx.get());
    const int depth = X509_STORE_CTX_get_error_depth(ctx.get());
    *error_details = absl::StrCat("X509_verify_cert: certificate verification error at depth ",
                                  depth, ": ", X509_verify_cert_error_string(n));
    return quic::QUIC_FAILURE;
  }

  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(certs[0]);
  ASSERT(cert_view != nullptr);
  for (const absl::string_view config_san : cert_view->subject_alt_name_domains()) {
    if (config_san == hostname) {
      return quic::QUIC_SUCCESS;
    }
  }
  *error_details = absl::StrCat("Leaf certificate doesn't match hostname: ", hostname);
  return quic::QUIC_FAILURE;
}

} // namespace Quic
} // namespace Envoy
