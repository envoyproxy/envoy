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
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

/**
 * All mysql proxy stats. @see stats_macros.h
 */
// clang-format off
#define ALL_MYSQL_PROXY_STATS(COUNTER)                                           \
  COUNTER(sessions)                                                              \
  COUNTER(login_attempts)                                                        \
  COUNTER(login_failures)                                                        \
  COUNTER(protocol_errors)                                                       \
  COUNTER(upgraded_to_ssl)                                                       \
  COUNTER(auth_switch_request)                                                   \
// clang-format on

/**
 * Struct definition for all mongo proxy stats. @see stats_macros.h
 */
struct MySQLProxyStats {
  ALL_MYSQL_PROXY_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the mysql proxy filter.
 */
class MySQLFilterConfig {
public:
  MySQLFilterConfig(const std::string &stat_prefix, Stats::Scope& scope);

  Stats::Scope& scope_;
  const std::string stat_prefix_;
  const MySQLProxyStats& stats() { return stats_; }
  MySQLProxyStats stats_;

private:
  MySQLProxyStats generateStats(const std::string& prefix,
                                Stats::Scope& scope) {
    return MySQLProxyStats{
        ALL_MYSQL_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
};

using MySQLFilterConfigSharedPtr = std::shared_ptr<MySQLFilterConfig>;

/**
 * Implementation of mysql network filter.
 */
class MySQLFilter : public Network::Filter, Logger::Loggable<Logger::Id::filter> {
 public:
   MySQLFilter(MySQLFilterConfigSharedPtr config);
   MySQLSession& getSession() { return session_; }
  ~MySQLFilter() override = default;

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
  MySQLFilterConfigSharedPtr config_;
  MySQLSession session_;
};

}  // namespace MySQLProxy
}  // namespace NetworkFilters
}  // namespace Extensions
}  // namespace Envoy
