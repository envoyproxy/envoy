#pragma once

#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/stats/symbol_table.h"

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
  COUNTER(ocsp_staple_requests)
/**
 * Wrapper struct for SSL stats. @see stats_macros.h
 */
struct SslStats {
  ALL_SSL_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

SslStats generateSslStats(Stats::Scope& store);

// The TLS stat name builtins (BoringSSL cipher/curve/signature algorithm/version names, plus the
// fixed prefixes, tag names and "unknown" fallbacks) are identical for every TLS context.
// Interning them into a per-context StatNameSet duplicated ~150 locked symbol-table encodes and a
// builtin map per context, and repeated it on every SDS certificate rotation. Instead we
// build the set once per server and share it via the singleton manager. The set is fully
// populated at construction and immutable thereafter, so the lock-free getBuiltin() reads
// performed on worker threads remain safe.
class TlsBuiltinStatNames : public Singleton::Instance {
public:
  explicit TlsBuiltinStatNames(Stats::SymbolTable& symbol_table);

  Stats::StatNameSet& statNameSet() const { return *stat_name_set_; }

  const Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName unknown_ssl_cipher_;
  const Stats::StatName unknown_ssl_curve_;
  const Stats::StatName unknown_ssl_algorithm_;
  const Stats::StatName unknown_ssl_version_;
  const Stats::StatName ssl_ciphers_;
  const Stats::StatName ssl_versions_;
  const Stats::StatName ssl_curves_;
  const Stats::StatName ssl_sigalgs_;
  // The names of the tags of the handshake stats. The cipher tag matches the
  // legacy extraction rules: "envoy.ssl_cipher" for the server (listener) side and "cipher_suite"
  // for the client (cluster) side.
  const Stats::StatName ssl_cipher_tag_name_;
  const Stats::StatName ssl_cipher_suite_tag_name_;
  const Stats::StatName ssl_curve_tag_name_;
  const Stats::StatName ssl_sigalg_tag_name_;
  const Stats::StatName ssl_version_tag_name_;
};

// Returns the server-wide shared TLS builtin stat names, creating them on first use. The set is
// owned by the singleton manager (per-server, hence tied to a single symbol table) and kept alive
// by the returned shared_ptr.
std::shared_ptr<TlsBuiltinStatNames>
getTlsBuiltinStatNames(Server::Configuration::CommonFactoryContext& factory_context);

Stats::Gauge& createCertificateExpirationGauge(Stats::Scope& scope, const std::string& cert_name);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
