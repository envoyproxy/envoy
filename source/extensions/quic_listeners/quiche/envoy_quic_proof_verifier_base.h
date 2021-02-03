#pragma once

#include "absl/strings/str_cat.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/quic_versions.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "common/common/logger.h"

namespace Envoy {
namespace Quic {

// A partial implementation of quic::ProofVerifier which does signature
// verification.
class EnvoyQuicProofVerifierBase : public quic::ProofVerifier,
                                   protected Logger::Loggable<Logger::Id::quic> {
public:
  ~EnvoyQuicProofVerifierBase() override = default;

  // quic::ProofVerifier
  // Return success if the certs chain is valid and signature of {
  // server_config + chlo_hash} is valid. Otherwise failure.
  quic::QuicAsyncStatus
  VerifyProof(const std::string& hostname, const uint16_t port, const std::string& server_config,
              quic::QuicTransportVersion /*quic_version*/, absl::string_view chlo_hash,
              const std::vector<std::string>& certs, const std::string& cert_sct,
              const std::string& signature, const quic::ProofVerifyContext* context,
              std::string* error_details, std::unique_ptr<quic::ProofVerifyDetails>* details,
              std::unique_ptr<quic::ProofVerifierCallback> callback) override;

  std::unique_ptr<quic::ProofVerifyContext> CreateDefaultContext() override { return nullptr; }

protected:
  virtual bool verifySignature(const std::string& server_config, absl::string_view chlo_hash,
                               const std::string& cert, const std::string& signature,
                               std::string* error_details);
};

} // namespace Quic
} // namespace Envoy
