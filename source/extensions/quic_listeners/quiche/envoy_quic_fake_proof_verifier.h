#pragma once

#include "absl/strings/str_cat.h"

#pragma GCC diagnostic push

// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/quic_versions.h"

#pragma GCC diagnostic pop

namespace Envoy {
namespace Quic {

// A fake implementation of quic::ProofVerifier which approves the certs and
// signature produced by EnvoyQuicFakeProofSource.
class EnvoyQuicFakeProofVerifier : public quic::ProofVerifier {
public:
  ~EnvoyQuicFakeProofVerifier() override = default;

  // quic::ProofVerifier
  // Return success if the certs chain is valid and signature is "Fake signature for {
  // [server_config] }". Otherwise failure.
  quic::QuicAsyncStatus
  VerifyProof(const std::string& hostname, const uint16_t port,
              const std::string& /*server_config*/, quic::QuicTransportVersion /*quic_version*/,
              absl::string_view /*chlo_hash*/, const std::vector<std::string>& certs,
              const std::string& cert_sct, const std::string& /*signature*/,
              const quic::ProofVerifyContext* context, std::string* error_details,
              std::unique_ptr<quic::ProofVerifyDetails>* details,
              std::unique_ptr<quic::ProofVerifierCallback> callback) override {
    if (VerifyCertChain(hostname, port, certs, "", cert_sct, context, error_details, details,
                        std::move(callback)) == quic::QUIC_SUCCESS) {
      return quic::QUIC_SUCCESS;
    }
    return quic::QUIC_FAILURE;
  }

  // Return success upon one arbitrary cert content. Otherwise failure.
  quic::QuicAsyncStatus
  VerifyCertChain(const std::string& /*hostname*/, const uint16_t /*port*/,
                  const std::vector<std::string>& certs, const std::string& /*ocsp_response*/,
                  const std::string& cert_sct, const quic::ProofVerifyContext* /*context*/,
                  std::string* /*error_details*/,
                  std::unique_ptr<quic::ProofVerifyDetails>* /*details*/,
                  std::unique_ptr<quic::ProofVerifierCallback> /*callback*/) override {
    // Cert SCT support is not enabled for fake ProofSource.
    if (cert_sct.empty() && certs.size() == 1) {
      return quic::QUIC_SUCCESS;
    }
    return quic::QUIC_FAILURE;
  }

  std::unique_ptr<quic::ProofVerifyContext> CreateDefaultContext() override { return nullptr; }
};

} // namespace Quic
} // namespace Envoy
