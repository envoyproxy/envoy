#pragma once
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
/**
 * All MySQL proxy stats. @see stats_macros.h
 */
#define ALL_MYSQL_PROXY_STATS(COUNTER)                                                             \
  COUNTER(sessions)                                                                                \
  COUNTER(login_attempts)                                                                          \
  COUNTER(login_failures)                                                                          \
  COUNTER(decoder_errors)                                                                          \
  COUNTER(protocol_errors)                                                                         \
  COUNTER(upgraded_to_ssl)                                                                         \
  COUNTER(auth_switch_request)                                                                     \
  COUNTER(queries_parsed)                                                                          \
  COUNTER(queries_parse_error)

/**
 * Struct definition for all MySQL proxy stats. @see stats_macros.h
 */
struct MySQLProxyStats {
  ALL_MYSQL_PROXY_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the MySQL proxy filter.
 */
class MySQLFilterConfig {
public:
  MySQLFilterConfig(const std::string& stat_prefix, Stats::Scope& scope)
      : scope_(scope), stats_(generateStats(stat_prefix, scope)) {}

  const MySQLProxyStats& stats() { return stats_; }

  Stats::Scope& scope_;
  MySQLProxyStats stats_;

  static MySQLProxyStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return MySQLProxyStats{ALL_MYSQL_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
};

using MySQLFilterConfigSharedPtr = std::shared_ptr<MySQLFilterConfig>;
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
