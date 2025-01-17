#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

#define ALL_CRYPTOMB_STATS(HISTOGRAM)                                                              \
  HISTOGRAM(ecdsa_queue_sizes, Unspecified)                                                        \
  HISTOGRAM(rsa_queue_sizes, Unspecified)

/**
 * CryptoMb stats struct definition. @see stats_macros.h
 */
struct CryptoMbStats {
  ALL_CRYPTOMB_STATS(GENERATE_HISTOGRAM_STRUCT)
};

CryptoMbStats generateCryptoMbStats(const std::string& prefix, Stats::Scope& scope);

} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
