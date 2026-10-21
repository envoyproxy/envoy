#include "source/common/tls/stats.h"

#include "envoy/singleton/manager.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/config/well_known_names.h"
#include "source/common/stats/symbol_table.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

SslStats generateSslStats(Stats::Scope& store) {
  std::string prefix("ssl.");
  return {ALL_SSL_STATS(POOL_COUNTER_PREFIX(store, prefix), POOL_GAUGE_PREFIX(store, prefix),
                        POOL_HISTOGRAM_PREFIX(store, prefix))};
}

SINGLETON_MANAGER_REGISTRATION(tls_builtin_stat_names);

TlsBuiltinStatNames::TlsBuiltinStatNames(Stats::SymbolTable& symbol_table)
    : stat_name_set_(symbol_table.makeSet("TransportSockets::Tls")),
      unknown_ssl_cipher_(stat_name_set_->add("unknown_ssl_cipher")),
      unknown_ssl_curve_(stat_name_set_->add("unknown_ssl_curve")),
      unknown_ssl_algorithm_(stat_name_set_->add("unknown_ssl_algorithm")),
      unknown_ssl_version_(stat_name_set_->add("unknown_ssl_version")),
      ssl_ciphers_(stat_name_set_->add("ssl.ciphers")),
      ssl_versions_(stat_name_set_->add("ssl.versions")),
      ssl_curves_(stat_name_set_->add("ssl.curves")),
      ssl_sigalgs_(stat_name_set_->add("ssl.sigalgs")),
      ssl_cipher_tag_name_(stat_name_set_->add(Envoy::Config::TagNames::get().SSL_CIPHER)),
      ssl_cipher_suite_tag_name_(
          stat_name_set_->add(Envoy::Config::TagNames::get().SSL_CIPHER_SUITE)),
      ssl_curve_tag_name_(stat_name_set_->add(Envoy::Config::TagNames::get().SSL_CURVE)),
      ssl_sigalg_tag_name_(stat_name_set_->add(Envoy::Config::TagNames::get().SSL_SIGALG)),
      ssl_version_tag_name_(stat_name_set_->add(Envoy::Config::TagNames::get().SSL_VERSION)) {
  // Register stat names based on lists reported by BoringSSL. These are compile-time constant
  // tables, so the resulting set is identical for every TLS context in the process.
  std::vector<const char*> list(SSL_get_all_cipher_names(nullptr, 0));
  SSL_get_all_cipher_names(list.data(), list.size());
  stat_name_set_->rememberBuiltins(list);

  list.resize(SSL_get_all_curve_names(nullptr, 0));
  SSL_get_all_curve_names(list.data(), list.size());
  stat_name_set_->rememberBuiltins(list);

  list.resize(SSL_get_all_signature_algorithm_names(nullptr, 0));
  SSL_get_all_signature_algorithm_names(list.data(), list.size());
  stat_name_set_->rememberBuiltins(list);

  list.resize(SSL_get_all_version_names(nullptr, 0));
  SSL_get_all_version_names(list.data(), list.size());
  stat_name_set_->rememberBuiltins(list);
}

std::shared_ptr<TlsBuiltinStatNames>
getTlsBuiltinStatNames(Server::Configuration::CommonFactoryContext& factory_context) {
  return factory_context.singletonManager().getTyped<TlsBuiltinStatNames>(
      SINGLETON_MANAGER_REGISTERED_NAME(tls_builtin_stat_names), [&factory_context] {
        return std::make_shared<TlsBuiltinStatNames>(factory_context.serverScope().symbolTable());
      });
}

Stats::Gauge& createCertificateExpirationGauge(Stats::Scope& scope, const std::string& cert_name) {
  // The constant names are interned in the scope's own symbol table rather than taken from the
  // shared TlsBuiltinStatNames: a StatName is only valid in the symbol table that encoded it, and
  // the caller's scope is not guaranteed to share a symbol table with the factory context that
  // owns the shared names (test harnesses wire them to separate stores). This only runs at config
  // load time, so the extra interning is off the hot path.
  Stats::SymbolTable& symbol_table = scope.symbolTable();
  Stats::StatNamePool pool(symbol_table);
  const Stats::StatName ssl_certificate = pool.add("ssl.certificate");
  const Stats::StatName expiration_unix_time_seconds = pool.add("expiration_unix_time_seconds");
  const Stats::StatNameDynamicStorage cert_stat_name(cert_name, symbol_table);

  // ssl.certificate.(<cert_name>.)expiration_unix_time_seconds: the certificate name is the tag
  // of the stat and leaves the tag-extracted name.
  const Stats::StatNameJoiner base_name({ssl_certificate, expiration_unix_time_seconds},
                                        symbol_table);
  const Stats::StatNameJoiner flat_name(
      {ssl_certificate, cert_stat_name.statName(), expiration_unix_time_seconds}, symbol_table);
  const Stats::StatNameTagVec tags{
      {pool.add(Envoy::Config::TagNames::get().TLS_CERTIFICATE), cert_stat_name.statName()}};
  return scope.gaugeFromTaggedName(base_name.statName(), tags, flat_name.statName(),
                                   Stats::Gauge::ImportMode::NeverImport);
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
