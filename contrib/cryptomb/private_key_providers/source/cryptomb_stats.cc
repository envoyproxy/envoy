#include "contrib/cryptomb/private_key_providers/source/cryptomb_stats.h"

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

CryptoMbStats generateCryptoMbStats(const std::string& prefix, Stats::Scope& scope) {
  return CryptoMbStats{ALL_CRYPTOMB_STATS(POOL_HISTOGRAM_PREFIX(scope, prefix))};
}

} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
