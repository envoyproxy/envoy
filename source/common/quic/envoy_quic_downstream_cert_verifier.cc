#include "source/common/quic/envoy_quic_downstream_cert_verifier.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_utils.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Quic {

quic::QuicAsyncStatus
verifyQuicClientCertChain(const std::vector<absl::string_view>& certs,
                          Extensions::TransportSockets::Tls::ContextImpl& context,
                          std::string* error_details,
                          std::unique_ptr<quic::ProofVerifyDetails>* details, uint8_t* out_alert) {
  ASSERT(error_details != nullptr);
  ASSERT(details != nullptr);

  auto fail = [error_details, details](absl::string_view msg) {
    ENVOY_LOG_MISC(debug, "QUIC client certificate validation rejected: {}", msg);
    *error_details = std::string(msg);
    *details = std::make_unique<CertVerifyResult>(false);
    return quic::QUIC_FAILURE;
  };

  if (certs.empty()) {
    return fail("client certificate required but not provided");
  }

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  for (size_t i = 0; i < certs.size(); ++i) {
    std::string parse_error;
    bssl::UniquePtr<X509> cert = parseDERCertificate(certs[i], &parse_error);
    if (cert == nullptr) {
      return fail(absl::StrCat("failed to parse client certificate ", i, ": ", parse_error));
    }
    if (!bssl::PushToStack(cert_chain.get(), std::move(cert))) {
      return fail("failed to push client certificate onto stack");
    }
  }

  // Pass an empty `host_name`. The QUIC SNI is the server's identity and forwarding it would cause
  // `auto_sni_san_match` to compare it against the client certificate SANs.
  Extensions::TransportSockets::Tls::CertValidator::ExtraValidationContext validation_ctx;
  validation_ctx.callbacks = nullptr;
  Extensions::TransportSockets::Tls::ValidationResults result =
      context.customVerifyCertChainForQuic(*cert_chain, /*callback=*/nullptr, /*is_server=*/true,
                                           /*transport_socket_options=*/nullptr, validation_ctx,
                                           /*host_name=*/std::string());

  if (result.status ==
      Extensions::TransportSockets::Tls::ValidationResults::ValidationStatus::Pending) {
    // Asynchronous validation is not wired through the QUIC handshaker path. Reject rather than
    // silently accepting the certificate.
    return fail("asynchronous client certificate validation is not supported on QUIC");
  }
  if (result.status !=
      Extensions::TransportSockets::Tls::ValidationResults::ValidationStatus::Successful) {
    if (result.tls_alert.has_value() && out_alert != nullptr) {
      *out_alert = result.tls_alert.value();
    }
    return fail(result.error_details.value_or("client certificate validation failed"));
  }

  ENVOY_LOG_MISC(debug, "QUIC client certificate validated, chain size {}", certs.size());
  *details = std::make_unique<CertVerifyResult>(true);
  return quic::QUIC_SUCCESS;
}

} // namespace Quic
} // namespace Envoy
