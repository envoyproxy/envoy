// NOLINT(namespace-envoy)

// This file defines platform dependent test utility functions which is declared
// in quiche/quic/test_tools/crypto_test_utils.h.

#include <memory>

#include "test/common/quic/test_proof_source.h"
#include "test/common/quic/test_proof_verifier.h"

#include "quiche/quic/test_tools/crypto_test_utils.h"

namespace quic {
namespace test {
namespace crypto_test_utils {
// NOLINTNEXTLINE(readability-identifier-naming)
std::unique_ptr<ProofSource> ProofSourceForTesting() {
  return std::make_unique<Envoy::Quic::TestProofSource>();
}

// NOLINTNEXTLINE(readability-identifier-naming)
std::unique_ptr<ProofVerifier> ProofVerifierForTesting() {
  return std::make_unique<Envoy::Quic::TestProofVerifier>();
}

// NOLINTNEXTLINE(readability-identifier-naming)
std::unique_ptr<ProofVerifyContext> ProofVerifyContextForTesting() {
  // No context needed for fake verifier.
  return nullptr;
}

} // namespace crypto_test_utils
} // namespace test
} // namespace quic
