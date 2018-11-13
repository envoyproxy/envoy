#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_loginok.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_query.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_srvresp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MysqlProxy {

/**
 * All mysql proxy stats. @see stats_macros.h
 */
// clang-format off
#define ALL_MYSQL_PROXY_STATS(COUNTER)                                           \
  COUNTER(new_sessions)                                                          \
  COUNTER(total_mysql_headers)                                                   \
  COUNTER(byte_count)                                                            \
  COUNTER(login_attempts)                                                        \
  COUNTER(login_failures)                                                        \
  COUNTER(total_queries)                                                         \
  COUNTER(query_failures)                                                        \
  COUNTER(wrong_sequence)                                                        \
  COUNTER(ssl_pass_through)                                                      \
  COUNTER(auth_switch_request)                                                   \
// clang-format on

/**
 * Struct definition for all mongo proxy stats. @see stats_macros.h
 */
struct MysqlProxyStats {
  ALL_MYSQL_PROXY_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the mysql proxy filter.
 */
class MysqlFilterConfig {
public:
  MysqlFilterConfig(const std::string &stat_prefix, Stats::Scope& scope);

  Stats::Scope& scope_;
  const std::string stat_prefix_;
  const MysqlProxyStats& stats() { return stats_; }
  MysqlProxyStats stats_;

private:
  MysqlProxyStats generateStats(const std::string& prefix,
                                Stats::Scope& scope) {
    return MysqlProxyStats{
        ALL_MYSQL_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
};

typedef std::shared_ptr<MysqlFilterConfig> MysqlFilterConfigSharedPtr;

/**
 * Implementation of mysql network filter.
 */
class MysqlFilter : public Network::Filter, Logger::Loggable<Logger::Id::filter> {
 public:
   MysqlFilter(MysqlFilterConfigSharedPtr config);
   MysqlSession& getSession() { return session_; }
  ~MysqlFilter() = default;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data,
                               bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(
      Network::ReadFilterCallbacks& callbacks) override;
  Network::FilterStatus onWrite(Buffer::Instance& data,
                                bool end_stream) override;

 private:
  Network::FilterStatus Process(Buffer::Instance& data, bool end_stream);
  Network::ReadFilterCallbacks* read_callbacks_{};
  MysqlFilterConfigSharedPtr config_;
  MysqlSession session_;
};

}  // namespace MysqlProxy
}  // namespace NetworkFilters
}  // namespace Extensions
}  // namespace Envoy
