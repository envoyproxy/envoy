#pragma once

#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "contrib/envoy/extensions/filters/network/smtp_proxy/v3alpha/smtp_proxy.pb.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

/**
 * All Smtp proxy stats. @see stats_macros.h
 */
#define ALL_SMTP_PROXY_STATS(COUNTER)                                                          \
  COUNTER(errors)                                                                                  \
  COUNTER(errors_error)                                                                            \
  COUNTER(errors_fatal)                                                                            \
  COUNTER(errors_panic)                                                                            \
  COUNTER(errors_unknown)                                                                          \
  COUNTER(messages)                                                                                \
  COUNTER(messages_backend)                                                                        \
  COUNTER(messages_frontend)                                                                       \
  COUNTER(messages_unknown)                                                                        \
  COUNTER(sessions)                                                                                \
  COUNTER(sessions_encrypted)                                                                      \
  COUNTER(sessions_terminated_ssl)                                                                 \
  COUNTER(sessions_unencrypted)                                                                    \
  COUNTER(sessions_upstream_ssl_success)                                                           \
  COUNTER(sessions_upstream_ssl_failed)                                                            \
  COUNTER(statements)                                                                              \
  COUNTER(statements_insert)                                                                       \
  COUNTER(statements_delete)                                                                       \
  COUNTER(statements_update)                                                                       \
  COUNTER(statements_select)                                                                       \
  COUNTER(statements_other)                                                                        \
  COUNTER(transactions)                                                                            \
  COUNTER(transactions_commit)                                                                     \
  COUNTER(transactions_rollback)                                                                   \
  COUNTER(statements_parsed)                                                                       \
  COUNTER(statements_parse_error)                                                                  \
  COUNTER(notices)                                                                                 \
  COUNTER(notices_notice)                                                                          \
  COUNTER(notices_warning)                                                                         \
  COUNTER(notices_debug)                                                                           \
  COUNTER(notices_info)                                                                            \
  COUNTER(notices_log)                                                                             \
  COUNTER(notices_unknown)

/**
 * Struct definition for all Smtp proxy stats. @see stats_macros.h
 */
struct SmtpProxyStats {
  ALL_SMTP_PROXY_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the Smtp proxy filter.
 */
class SmtpFilterConfig {
public:
  struct SmtpFilterConfigOptions {
    std::string stats_prefix_;
    bool enable_sql_parsing_;
    bool terminate_ssl_;
    envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::SSLMode
        upstream_ssl_;
  };
  SmtpFilterConfig(const SmtpFilterConfigOptions& config_options, Stats::Scope& scope);

  bool enable_sql_parsing_{true};
  bool terminate_ssl_{false};
  envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::SSLMode
      upstream_ssl_{
          envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::DISABLE};
  Stats::Scope& scope_;
  SmtpProxyStats stats_;

private:
  SmtpProxyStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return SmtpProxyStats{ALL_SMTP_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
};

using SmtpFilterConfigSharedPtr = std::shared_ptr<SmtpFilterConfig>;

class SmtpFilter : public Network::Filter,
                       DecoderCallbacks,
                       Logger::Loggable<Logger::Id::filter> {
public:
  SmtpFilter(SmtpFilterConfigSharedPtr config);
  ~SmtpFilter() override = default;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

  // SmtpProxy::DecoderCallback
  void incErrors(ErrorType) override;
  void incMessagesBackend() override;
  void incMessagesFrontend() override;
  void incMessagesUnknown() override;
  void incNotices(NoticeType) override;
  void incSessionsEncrypted() override;
  void incSessionsUnencrypted() override;
  void incStatements(StatementType) override;
  void incTransactions() override;
  void incTransactionsCommit() override;
  void incTransactionsRollback() override;
  void processQuery(const std::string&) override;
  bool onSSLRequest() override;
  bool shouldEncryptUpstream() const override;
  void sendUpstream(Buffer::Instance&) override;
  void encryptUpstream(bool, Buffer::Instance&) override;

  Network::FilterStatus doDecode(Buffer::Instance& data, bool);
  DecoderPtr createDecoder(DecoderCallbacks* callbacks);
  void setDecoder(std::unique_ptr<Decoder> decoder) { decoder_ = std::move(decoder); }
  Decoder* getDecoder() const { return decoder_.get(); }

  // Routines used during integration and unit tests
  uint32_t getFrontendBufLength() const { return frontend_buffer_.length(); }
  uint32_t getBackendBufLength() const { return backend_buffer_.length(); }
  const SmtpProxyStats& getStats() const { return config_->stats_; }
  Network::Connection& connection() const { return read_callbacks_->connection(); }
  const SmtpFilterConfigSharedPtr& getConfig() const { return config_; }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  SmtpFilterConfigSharedPtr config_;
  Buffer::OwnedImpl frontend_buffer_;
  Buffer::OwnedImpl backend_buffer_;
  std::unique_ptr<Decoder> decoder_;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
