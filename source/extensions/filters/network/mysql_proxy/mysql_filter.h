#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/api/api.h"
#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/filters/network/mysql_proxy/new_conn_pool_impl.h"
#include "extensions/filters/network/mysql_proxy/mysql_client.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"
#include "extensions/filters/network/mysql_proxy/route.h"

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
  MySQLFilterConfig(Stats::Scope& scope,
                    const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy& config,
                    Api::Api& api);

  const MySQLProxyStats& stats() { return stats_; }

  MySQLProxyStats stats_;
  std::string username_;
  std::string password_;

  static MySQLProxyStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return MySQLProxyStats{ALL_MYSQL_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
};

using MySQLFilterConfigSharedPtr = std::shared_ptr<MySQLFilterConfig>;

/**
 * Implementation of MySQL proxy filter.
 */
class MySQLFilter : public Network::ReadFilter,
                    public DecoderCallbacks,
                    public ClientCallBack,
                    public ConnPool::ClientPoolCallBack,
                    public Network::ConnectionCallbacks,
                    public Logger::Loggable<Logger::Id::filter> {
public:
  MySQLFilter(MySQLFilterConfigSharedPtr config, RouterSharedPtr router,
              ClientFactory& client_factory, DecoderFactory& decoder_factory);
  ~MySQLFilter() override = default;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

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

  // ConnPool::ClientPoolCallBack
  void onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                   Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolFailure(ConnPool::MySQLPoolFailureReason,
                     Upstream::HostDescriptionConstSharedPtr host) override;

  // ClientCallBack
  void onResponse(MySQLCodec&, uint8_t seq) override;
  void onFailure() override;

  void doDecode(Buffer::Instance& buffer);
  DecoderPtr createDecoder(DecoderCallbacks& callbacks);
  MySQLSession& getSession() { return decoder_->getSession(); }
  friend class MySQLFilterTest;

private:
  void onFailure(const ClientLoginResponse& err, uint8_t seq);
  void onAuthOk();

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  MySQLFilterConfigSharedPtr config_;
  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
  DecoderPtr decoder_;
  RouterSharedPtr router_;
  ClientFactory& client_factory_;
  DecoderFactory& decoder_factory_;
  ConnectionPool::Cancellable* canceler_{nullptr};
  ClientPtr client_;
  std::vector<uint8_t> seed_;
  bool authed_{false};
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
