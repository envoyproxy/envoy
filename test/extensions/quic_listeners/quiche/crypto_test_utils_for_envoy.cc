// NOLINT(namespace-envoy)

// This file defines platform dependent test utility functions which is declared
// in quiche/quic/test_tools/crypto_test_utils.h.

#ifdef __GNUC__
#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#pragma GCC diagnostic ignored "-Wtype-limits"

#include "quiche/quic/test_tools/crypto_test_utils.h"

#pragma GCC diagnostic pop
#else
#include "quiche/quic/test_tools/crypto_test_utils.h"
#endif

#include <memory>
#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_source.h"
#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_verifier.h"

namespace quic {
namespace test {
namespace crypto_test_utils {
std::unique_ptr<ProofSource> ProofSourceForTesting() {
  return std::make_unique<Envoy::Quic::EnvoyQuicFakeProofSource>();
}

std::unique_ptr<ProofVerifier> ProofVerifierForTesting() {
  return std::make_unique<Envoy::Quic::EnvoyQuicFakeProofVerifier>();
}

std::unique_ptr<ProofVerifyContext> ProofVerifyContextForTesting() {
  // No context needed for fake verifier.
  return nullptr;
}

} // namespace crypto_test_utils
} // namespace test
} // namespace quic
