#pragma once

#include <memory>
#include <string>
#include <vector>

#include "source/common/quic/envoy_quic_proof_verifier_base.h"
#include "source/common/tls/context_impl.h"

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_verifier.h"

namespace Envoy {
namespace Quic {

// Validates a downstream client certificate chain presented during a QUIC handshake. The
// DER-encoded `certs` are checked against `context`, the server context of the connection's matched
// filter chain, so validation always uses the trust anchor of the chain that requested the
// certificate. Sets `*error_details` and `*details`, sets `*out_alert` when a TLS alert is
// available, and returns `QUIC_SUCCESS` or `QUIC_FAILURE`.
quic::QuicAsyncStatus verifyQuicClientCertChain(
    const std::vector<absl::string_view>& certs,
    Extensions::TransportSockets::Tls::ContextImpl& context, std::string* error_details,
    std::unique_ptr<quic::ProofVerifyDetails>* details, uint8_t* out_alert = nullptr);

// Fail-closed server proof verifier installed on the QUIC crypto config as a safety net. The
// default crypto stream uses `EnvoyTlsServerHandshaker`, which overrides `VerifyCertChain` and
// never consults this verifier. It is reached only if a custom crypto stream leaves client
// certificate validation to the shared verifier, in which case the certificate is rejected rather
// than accepted without validation.
class EnvoyQuicServerFailClosedProofVerifier : public EnvoyQuicProofVerifierBase {
public:
  // quic::ProofVerifier
  quic::QuicAsyncStatus
  VerifyCertChain(const std::string& hostname, const uint16_t port,
                  const std::vector<absl::string_view>& certs, const std::string& ocsp_response,
                  const std::string& cert_sct, const quic::ProofVerifyContext* context,
                  std::string* error_details, std::unique_ptr<quic::ProofVerifyDetails>* details,
                  uint8_t* out_alert,
                  std::unique_ptr<quic::ProofVerifierCallback> callback) override;
};

} // namespace Quic
} // namespace Envoy
