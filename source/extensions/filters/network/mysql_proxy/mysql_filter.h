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
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"

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
  MySQLFilterConfig(const std::string& stat_prefix, Stats::Scope& scope);

  const MySQLProxyStats& stats() { return stats_; }

  Stats::Scope& scope_;
  const std::string stat_prefix_;
  MySQLProxyStats stats_;

private:
  MySQLProxyStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return MySQLProxyStats{ALL_MYSQL_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
};

using MySQLFilterConfigSharedPtr = std::shared_ptr<MySQLFilterConfig>;

/**
 * Implementation of MySQL proxy filter.
 */
class MySQLFilter : public Network::Filter, DecoderCallbacks, Logger::Loggable<Logger::Id::filter> {
public:
  MySQLFilter(MySQLFilterConfigSharedPtr config);
  ~MySQLFilter() override = default;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

  // MySQLProxy::DecoderCallback
  void onProtocolError() override;
  void onNewMessage(MySQLSession::State state) override;
  void onServerGreeting(ServerGreeting&) override{};
  void onClientLogin(ClientLogin& message) override;
  void onClientLoginResponse(ClientLoginResponse& message) override;
  void onClientSwitchResponse(ClientSwitchResponse&) override{};
  void onMoreClientLoginResponse(ClientLoginResponse& message) override;
  void onCommand(Command& message) override;
  void onCommandResponse(CommandResponse&) override{};

  void doDecode(Buffer::Instance& buffer);
  DecoderPtr createDecoder(DecoderCallbacks& callbacks);
  MySQLSession& getSession() { return decoder_->getSession(); }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  MySQLFilterConfigSharedPtr config_;
  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
  std::unique_ptr<Decoder> decoder_;
  bool sniffing_{true};
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
