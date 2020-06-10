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
#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_verifier.h"
#include "test/extensions/quic_listeners/quiche/test_proof_source.h"

namespace quic {
namespace test {
namespace crypto_test_utils {
// NOLINTNEXTLINE(readability-identifier-naming)
std::unique_ptr<ProofSource> ProofSourceForTesting() {
  return std::make_unique<Envoy::Quic::TestProofSource>();
}

// NOLINTNEXTLINE(readability-identifier-naming)
std::unique_ptr<ProofVerifier> ProofVerifierForTesting() {
  return std::make_unique<Envoy::Quic::EnvoyQuicFakeProofVerifier>();
}

// NOLINTNEXTLINE(readability-identifier-naming)
std::unique_ptr<ProofVerifyContext> ProofVerifyContextForTesting() {
  // No context needed for fake verifier.
  return nullptr;
}

} // namespace crypto_test_utils
} // namespace test
} // namespace quic
