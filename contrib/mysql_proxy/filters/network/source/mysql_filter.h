#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"

#include "contrib/envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin_resp.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_command.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_greeting.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_switch_resp.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_decoder.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_session.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"

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
  using SSLMode = envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::SSLMode;

  MySQLFilterConfig(const std::string& stat_prefix, Stats::Scope& scope, SSLMode downstream_ssl);

  const MySQLProxyStats& stats() { return stats_; }
  bool terminateSsl() const {
    return downstream_ssl_ !=
           envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::DISABLE;
  }

  Stats::Scope& scope_;
  MySQLProxyStats stats_;
  SSLMode downstream_ssl_;

private:
  MySQLProxyStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return MySQLProxyStats{ALL_MYSQL_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
};

using MySQLFilterConfigSharedPtr = std::shared_ptr<MySQLFilterConfig>;

enum class RsaAuthState {
  Inactive,              // Normal operation
  WaitingClientPassword, // Server sent AuthMoreData(0x04), forwarded to client, waiting for pw
  WaitingServerKey,      // Sent 0x02 to server, waiting for PEM public key
  WaitingServerResult,   // Sent RSA-encrypted pw, waiting for OK/ERR
};

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
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override;

  // MySQLProxy::DecoderCallback
  void onProtocolError() override;
  void onServerGreeting(ServerGreeting& greeting) override;
  void onClientLogin(ClientLogin& message, MySQLSession::State state) override;
  void onClientLoginResponse(ClientLoginResponse& message) override;
  void onClientSwitchResponse(ClientSwitchResponse&) override {};
  void onMoreClientLoginResponse(ClientLoginResponse& message) override;
  void onCommand(Command& message) override;
  void onCommandResponse(CommandResponse&) override {};
  void onAuthSwitchMoreClientData(std::unique_ptr<SecureBytes> data) override;
  bool onSSLRequest() override;

  Network::FilterStatus doDecode(Buffer::Instance& buffer, bool is_upstream);
  DecoderPtr createDecoder(DecoderCallbacks& callbacks);
  void doRewrite(Buffer::Instance& buffer, uint64_t remaining, bool is_upstream);
  MySQLSession& getSession() { return decoder_->getSession(); }

  // Helpers for doRewrite.
  static void rewritePacketHeader(Buffer::Instance& data, uint8_t seq, uint32_t len);
  static void stripSslCapability(Buffer::Instance& data);

  RsaAuthState getRsaAuthState() const { return rsa_auth_state_; }

private:
  void sendEncryptedPassword(const std::string& pem_key, uint8_t last_server_seq);

  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};
  MySQLFilterConfigSharedPtr config_;
  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
  std::unique_ptr<Decoder> decoder_;
  bool sniffing_{true};

  // RSA mediation state for caching_sha2_password full authentication.
  RsaAuthState rsa_auth_state_{RsaAuthState::Inactive};
  std::unique_ptr<SecureBytes> cleartext_password_;
  std::vector<uint8_t> server_scramble_;
  std::string auth_plugin_name_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
