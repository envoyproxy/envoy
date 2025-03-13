#include "source/common/tls/stats.h"

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

SslStats generateSslStats(Stats::Scope& store) {
  std::string prefix("ssl.");
  return {ALL_SSL_STATS(POOL_COUNTER_PREFIX(store, prefix), POOL_GAUGE_PREFIX(store, prefix),
                        POOL_HISTOGRAM_PREFIX(store, prefix))};
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
