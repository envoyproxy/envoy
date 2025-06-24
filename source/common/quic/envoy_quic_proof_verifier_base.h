#pragma once

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

#include "absl/strings/str_cat.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/quic_versions.h"

namespace Envoy {
namespace Quic {

// A partial implementation of quic::ProofVerifier which provides stub
// implementations for Google QUIC methods which are not supported by Envoy.
class EnvoyQuicProofVerifierBase : public quic::ProofVerifier,
                                   protected Logger::Loggable<Logger::Id::quic> {
public:
  ~EnvoyQuicProofVerifierBase() override = default;

  // quic::ProofVerifier
  // This method is only used in Google QUIC which is not supported by Envoy.
  quic::QuicAsyncStatus
  VerifyProof(const std::string& /*hostname*/, const uint16_t /*port*/,
              const std::string& /*server_config*/, quic::QuicTransportVersion /*quic_version*/,
              absl::string_view /*chlo_hash*/, const std::vector<std::string>& /*certs*/,
              const std::string& /*cert_sct*/, const std::string& /*signature*/,
              const quic::ProofVerifyContext* /*context*/, std::string* /*error_details*/,
              std::unique_ptr<quic::ProofVerifyDetails>* /*details*/,
              std::unique_ptr<quic::ProofVerifierCallback> /*callback*/) override {
    PANIC("not implemented");
  }

  std::unique_ptr<quic::ProofVerifyContext> CreateDefaultContext() override { return nullptr; }
};

} // namespace Quic
} // namespace Envoy
