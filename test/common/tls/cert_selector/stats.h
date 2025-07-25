#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

#define ALL_CERT_SELECTION_STATS(COUNTER, GAUGE, HISTOGRAM)                                        \
  COUNTER(cert_selection_sync)                                                                     \
  COUNTER(cert_selection_async)                                                                    \
  COUNTER(cert_selection_async_finished)                                                           \
  COUNTER(cert_selection_sleep)                                                                    \
  COUNTER(cert_selection_sleep_finished)                                                           \
  COUNTER(cert_selection_failed)

/**
 * Wrapper struct for SSL stats. @see stats_macros.h
 */
struct CertSelectionStats {
  ALL_CERT_SELECTION_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                           GENERATE_HISTOGRAM_STRUCT)
};

CertSelectionStats generateCertSelectionStats(Stats::Scope& store);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
