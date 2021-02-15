#pragma once

#include "extensions/quic_listeners/quiche/envoy_quic_proof_verifier_base.h"
#include "extensions/transport_sockets/tls/cert_validator/default_validator.h"
#include "extensions/transport_sockets/tls/context_impl.h"

namespace Envoy {
namespace Quic {

// A quic::ProofVerifier implementation which verifies cert chain using SSL
// client context config.
class EnvoyQuicProofVerifier : public EnvoyQuicProofVerifierBase {
public:
  EnvoyQuicProofVerifier(Stats::Scope& scope, const Envoy::Ssl::ClientContextConfig& config,
                         TimeSource& time_source)
      : context_impl_(scope, config, time_source) {}

  // EnvoyQuicProofVerifierBase
  quic::QuicAsyncStatus
  VerifyCertChain(const std::string& hostname, const uint16_t port,
                  const std::vector<std::string>& certs, const std::string& ocsp_response,
                  const std::string& cert_sct, const quic::ProofVerifyContext* context,
                  std::string* error_details, std::unique_ptr<quic::ProofVerifyDetails>* details,
                  std::unique_ptr<quic::ProofVerifierCallback> callback) override;

private:
  Extensions::TransportSockets::Tls::ClientContextImpl context_impl_;
};

} // namespace Quic
} // namespace Envoy
