#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

#define ALL_SSL_STATS(COUNTER, GAUGE, HISTOGRAM)                                                   \
  COUNTER(connection_error)                                                                        \
  COUNTER(handshake)                                                                               \
  COUNTER(session_reused)                                                                          \
  COUNTER(no_certificate)                                                                          \
  COUNTER(fail_verify_no_cert)                                                                     \
  COUNTER(fail_verify_error)                                                                       \
  COUNTER(fail_verify_san)                                                                         \
  COUNTER(fail_verify_cert_hash)                                                                   \
  COUNTER(ocsp_staple_failed)                                                                      \
  COUNTER(ocsp_staple_omitted)                                                                     \
  COUNTER(ocsp_staple_responses)                                                                   \
  COUNTER(ocsp_staple_requests)                                                                    \
  COUNTER(was_key_usage_invalid)

/**
 * Wrapper struct for SSL stats. @see stats_macros.h
 */
struct SslStats {
  ALL_SSL_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

SslStats generateSslStats(Stats::Scope& store);

#define ALL_CERT_STATS(GAUGE) GAUGE(expiration_unix_time_in_seconds, NeverImport)

struct CertStats {
  ALL_CERT_STATS(GENERATE_GAUGE_STRUCT)
};

CertStats generateCertStats(Stats::Scope& scope, std::string cert_name);

using CertStatsPtr = std::unique_ptr<CertStats>;

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
