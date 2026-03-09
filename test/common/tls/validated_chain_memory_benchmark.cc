// Benchmark to measure per-connection memory overhead of storing the validated
// certificate chain (std::vector<bssl::UniquePtr<X509>> obtained via X509_up_ref).
//
// We allocate N chains, each containing ref-counted copies of real X509 certs,
// and measure heap growth to compute per-chain cost.

#include <vector>

#include "source/common/memory/stats.h"

#include "benchmark/benchmark.h"
#include "openssl/pem.h"
#include "openssl/x509.h"

namespace Envoy {

// Inline PEM certificate data (self-signed test CA, ~2KB). Using inline data
// avoids the need for runfiles / TestEnvironment setup in the benchmark main.
// This cert is equivalent in structure/size to the certs used in Envoy's test
// suite (RSA 2048 leaf + CA).

// Generate a self-signed X509 certificate in memory (no file I/O needed).
static bssl::UniquePtr<X509> generateSelfSignedCert() {
  bssl::UniquePtr<EVP_PKEY> pkey(EVP_PKEY_new());
  bssl::UniquePtr<RSA> rsa(RSA_new());
  bssl::UniquePtr<BIGNUM> bn(BN_new());
  BN_set_word(bn.get(), RSA_F4);
  RSA_generate_key_ex(rsa.get(), 2048, bn.get(), nullptr);
  EVP_PKEY_assign_RSA(pkey.get(), rsa.release());

  bssl::UniquePtr<X509> cert(X509_new());
  X509_set_version(cert.get(), 2); // V3
  ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
  X509_gmtime_adj(X509_get_notBefore(cert.get()), 0);
  X509_gmtime_adj(X509_get_notAfter(cert.get()), 365 * 24 * 3600);
  X509_set_pubkey(cert.get(), pkey.get());

  X509_NAME* name = X509_get_subject_name(cert.get());
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>("benchmark-cert"), -1, -1, 0);
  X509_set_issuer_name(cert.get(), name);
  X509_sign(cert.get(), pkey.get(), EVP_sha256());

  return cert;
}

// Simulate what the implementation does: for each connection, store a
// vector<UniquePtr<X509>> where each X509* is obtained via X509_up_ref
// (reference count bump, no deep copy).
static void BM_ValidatedChainMemoryOverhead(benchmark::State& state) {
  const int64_t num_chains = state.range(0);

  // Generate three certs to simulate a typical validated chain
  // (leaf + intermediate + root).
  bssl::UniquePtr<X509> leaf = generateSelfSignedCert();
  bssl::UniquePtr<X509> intermediate = generateSelfSignedCert();
  bssl::UniquePtr<X509> root = generateSelfSignedCert();
  RELEASE_ASSERT(leaf != nullptr, "failed to generate leaf cert");
  RELEASE_ASSERT(intermediate != nullptr, "failed to generate intermediate cert");
  RELEASE_ASSERT(root != nullptr, "failed to generate root cert");

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);

    // Measure memory before allocation.
    const uint64_t mem_before = Memory::Stats::totalCurrentlyAllocated();

    // Allocate N chains, each holding 3 ref-counted certs
    // (leaf + intermediate + root).
    std::vector<std::vector<bssl::UniquePtr<X509>>> chains;
    chains.reserve(num_chains);
    for (int64_t i = 0; i < num_chains; ++i) {
      std::vector<bssl::UniquePtr<X509>> chain;
      chain.reserve(3);
      // Bump reference count (same as real code path).
      X509_up_ref(leaf.get());
      chain.emplace_back(leaf.get());
      X509_up_ref(intermediate.get());
      chain.emplace_back(intermediate.get());
      X509_up_ref(root.get());
      chain.emplace_back(root.get());
      chains.push_back(std::move(chain));
    }

    const uint64_t mem_after = Memory::Stats::totalCurrentlyAllocated();
    const int64_t total_bytes = static_cast<int64_t>(mem_after - mem_before);

    state.counters["total_bytes"] = total_bytes;
    state.counters["bytes_per_chain"] =
        benchmark::Counter(static_cast<double>(total_bytes) / static_cast<double>(num_chains));

    // Prevent the compiler from optimizing away the chains.
    benchmark::DoNotOptimize(chains);
    // Chains are freed here at end of scope.
  }
}

BENCHMARK(BM_ValidatedChainMemoryOverhead)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMillisecond);

} // namespace Envoy
