#pragma once

#include "source/common/quic/envoy_quic_proof_verifier_base.h"

namespace Envoy {
namespace Quic {

// A test quic::ProofVerifier which always approves the certs.
class TestProofVerifier : public EnvoyQuicProofVerifierBase {
public:
  //  quic::ProofVerifier
  quic::QuicAsyncStatus
  VerifyCertChain(const std::string& /*hostname*/, const uint16_t /*port*/,
                  const std::vector<std::string>& /*certs*/, const std::string& /*ocsp_response*/,
                  const std::string& /*cert_sct*/, const quic::ProofVerifyContext* /*context*/,
                  std::string* /*error_details*/,
                  std::unique_ptr<quic::ProofVerifyDetails>* /*details*/, uint8_t* /*out_alert*/,
                  std::unique_ptr<quic::ProofVerifierCallback> /*callback*/) override {
    return quic::QUIC_SUCCESS;
  }
};

} // namespace Quic
} // namespace Envoy
