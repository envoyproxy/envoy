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

CertStats generateCertStats(Stats::Scope& scope, std::string cert_name) {
  std::string prefix_cert_name(absl::StrCat("ssl.certificate.", cert_name, "."));
  return {ALL_CERT_STATS(POOL_GAUGE_PREFIX(scope, prefix_cert_name))};
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
