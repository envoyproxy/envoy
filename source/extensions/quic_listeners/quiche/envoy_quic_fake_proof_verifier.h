#pragma once

#pragma GCC diagnostic push

// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/quic_versions.h"

#pragma GCC diagnostic pop

namespace Envoy {
namespace Quic {

// A dummy implementation of quic::ProofVerifier which approves the certs and
// signature produced by EnvoyQuicFakeProofSource only.
class EnvoyQuicFakeProofVerifier : public quic::ProofVerifier {
public:
  EnvoyQuicFakeProofVerifier() {}
  ~EnvoyQuicFakeProofVerifier() override {}

  // quic::ProofVerifier
  quic::QuicAsyncStatus
  VerifyProof(const std::string& hostname, const uint16_t /*port*/,
              const std::string& server_config, quic::QuicTransportVersion /*quic_version*/,
              absl::string_view /*chlo_hash*/, const std::vector<std::string>& certs,
              const std::string& cert_sct, const std::string& signature,
              const quic::ProofVerifyContext* context, std::string* error_details,
              std::unique_ptr<quic::ProofVerifyDetails>* details,
              std::unique_ptr<quic::ProofVerifierCallback> callback) override {
    if (VerifyCertChain(hostname, certs, "", cert_sct, context, error_details, details,
                        std::move(callback)) == quic::QUIC_SUCCESS &&
        signature == absl::StrCat("Dummy signature for { ", server_config, " }")) {
      return quic::QUIC_SUCCESS;
    }
    return quic::QUIC_FAILURE;
  }

  quic::QuicAsyncStatus
  VerifyCertChain(const std::string& hostname, const std::vector<std::string>& certs,
                  const std::string& /*ocsp_response*/, const std::string& cert_sct,
                  const quic::ProofVerifyContext* /*context*/, std::string* /*error_details*/,
                  std::unique_ptr<quic::ProofVerifyDetails>* /*details*/,
                  std::unique_ptr<quic::ProofVerifierCallback> /*callback*/) override {
    std::string cert = absl::StrCat(dummy_cert_prefix, hostname);
    if (cert_sct == "Dummy timestamp" && certs.size() == 1 && certs[0] == cert) {
      return quic::QUIC_SUCCESS;
    }
    return quic::QUIC_FAILURE;
  }

  std::unique_ptr<quic::ProofVerifyContext> CreateDefaultContext() override { return nullptr; }
};

} // namespace Quic
} // namespace Envoy
