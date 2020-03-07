#pragma once

#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

/**
 * All PostgreSQL proxy stats. @see stats_macros.h
 */
#define ALL_POSTGRESQL_PROXY_STATS(COUNTER)                                                        \
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
 * Struct definition for all PostgreSQL proxy stats. @see stats_macros.h
 */
struct PostgreSQLProxyStats {
  ALL_POSTGRESQL_PROXY_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the PostgreSQL proxy filter.
 */
class PostgreSQLFilterConfig {
public:
  PostgreSQLFilterConfig(const std::string& stat_prefix, Stats::Scope& scope);

  const std::string stat_prefix_;
  Stats::Scope& scope_;
  PostgreSQLProxyStats stats_;

private:
  PostgreSQLProxyStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return PostgreSQLProxyStats{ALL_POSTGRESQL_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
};

using PostgreSQLFilterConfigSharedPtr = std::shared_ptr<PostgreSQLFilterConfig>;

class PostgreSQLFilter : public Network::Filter, Logger::Loggable<Logger::Id::filter> {
public:
  PostgreSQLFilter(PostgreSQLFilterConfigSharedPtr config);
  ~PostgreSQLFilter() override = default;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  PostgreSQLFilterConfigSharedPtr config_;
};

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
