#pragma once

#include "quiche/quic/core/crypto/proof_verifier.h"

namespace Envoy {

class EnvoyQuicDummyProofVerifier : public quic::ProofVerifier {
public:
  EnvoyQuicFakeProofVerifier() {}
  ~EnvoyQuicFakeProofVerifier() override {}

  quic::QuicAsyncStatus
  VerifyProof(const string& /*hostname*/, const uint16_t /*port*/, const string& /*server_config*/,
              quic::QuicTransportVersion /*quic_version*/, absl::string_view /*chlo_hash*/,
              const std::vector<string>& /*certs*/, const string& /*cert_sct*/,
              const string& /*signature*/, const quic::ProofVerifyContext* /*context*/,
              string* /*error_details*/, std::unique_ptr<quic::ProofVerifyDetails>* /*details*/,
              std::unique_ptr<quic::ProofVerifierCallback> /*callback*/) override {
    return quic::QUIC_SUCCESS;
  }
  quic::QuicAsyncStatus
  VerifyCertChain(const string& /*hostname*/, const std::vector<string>& /*certs*/,
                  const quic::ProofVerifyContext* /*context*/, string* /*error_details*/,
                  std::unique_ptr<quic::ProofVerifyDetails>* /*details*/,
                  std::unique_ptr<quic::ProofVerifierCallback> /*callback*/) override {
    return quic::QUIC_SUCCESS;
  }
  std::unique_ptr<quic::ProofVerifyContext> CreateDefaultContext() override { return nullptr; }
};

} // namespace Envoy
