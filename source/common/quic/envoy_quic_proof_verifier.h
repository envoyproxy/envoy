#pragma once

#include "source/common/quic/envoy_quic_proof_verifier_base.h"
#include "source/extensions/transport_sockets/tls/context_impl.h"

namespace Envoy {
namespace Quic {

// A quic::ProofVerifier implementation which verifies cert chain using SSL
// client context config.
class EnvoyQuicProofVerifier : public EnvoyQuicProofVerifierBase {
public:
  EnvoyQuicProofVerifier(Envoy::Ssl::ClientContextSharedPtr&& context)
      : context_(std::move(context)) {
    ASSERT(context_.get());
  }

  // EnvoyQuicProofVerifierBase
  quic::QuicAsyncStatus
  VerifyCertChain(const std::string& hostname, const uint16_t port,
                  const std::vector<std::string>& certs, const std::string& ocsp_response,
                  const std::string& cert_sct, const quic::ProofVerifyContext* context,
                  std::string* error_details, std::unique_ptr<quic::ProofVerifyDetails>* details,
                  uint8_t* out_alert,
                  std::unique_ptr<quic::ProofVerifierCallback> callback) override;

private:
  Envoy::Ssl::ClientContextSharedPtr context_;
};

} // namespace Quic
} // namespace Envoy
