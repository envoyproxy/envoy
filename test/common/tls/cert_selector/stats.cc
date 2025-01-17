#include "test/common/tls/cert_selector/stats.h"

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

CertSelectionStats generateCertSelectionStats(Stats::Scope& store) {
  std::string prefix("aysnc_cert_selection.");
  return {ALL_CERT_SELECTION_STATS(POOL_COUNTER_PREFIX(store, prefix),
                                   POOL_GAUGE_PREFIX(store, prefix),
                                   POOL_HISTOGRAM_PREFIX(store, prefix))};
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
