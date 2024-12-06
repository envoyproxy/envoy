#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/common/random_generator.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/common/utility.h"

#include "contrib/envoy/extensions/filters/network/smtp_proxy/v3alpha/smtp_proxy.pb.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_decoder_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

/**
 * All SMTP proxy stats. @see stats_macros.h
 */
#define ALL_SMTP_PROXY_STATS(COUNTER)                                                              \
  COUNTER(smtp_session_requests)                                                                   \
  COUNTER(smtp_connection_establishment_errors)                                                    \
  COUNTER(smtp_sessions_completed)                                                                 \
  COUNTER(smtp_sessions_terminated)                                                                \
  COUNTER(smtp_transactions)                                                                       \
  COUNTER(smtp_transactions_aborted)                                                               \
  COUNTER(smtp_tls_terminated_sessions)                                                            \
  COUNTER(smtp_tls_termination_errors)                                                             \
  COUNTER(sessions_upstream_tls_success)                                                           \
  COUNTER(sessions_upstream_tls_failed)                                                            \
  COUNTER(smtp_auth_errors)                                                                        \
  COUNTER(smtp_mail_data_transfer_errors)                                                          \
  COUNTER(smtp_rcpt_errors)

/**
 * Struct definition for all SMTP proxy stats. @see stats_macros.h
 */
struct SmtpProxyStats {
  ALL_SMTP_PROXY_STATS(GENERATE_COUNTER_STRUCT)
};

class SmtpFilterConfig {
public:
  struct SmtpFilterConfigOptions {
    std::string stats_prefix_;
    bool tracing_;
    envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::UpstreamTLSMode
        upstream_tls_;
    std::vector<AccessLog::InstanceSharedPtr> access_logs_;
  };
  SmtpFilterConfig(const SmtpFilterConfigOptions& config_options, Stats::Scope& scope);
  const SmtpProxyStats& stats() { return stats_; }
  const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const { return access_logs_; }
  Stats::Scope& scope_;
  SmtpProxyStats stats_;
  bool tracing_;
  std::vector<AccessLog::InstanceSharedPtr> access_logs_;
  envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::UpstreamTLSMode
      upstream_tls_{envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::DISABLE};

private:
  SmtpProxyStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return SmtpProxyStats{ALL_SMTP_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
};

using SmtpFilterConfigSharedPtr = std::shared_ptr<SmtpFilterConfig>;

class SmtpFilter : public Network::Filter, DecoderCallbacks, Logger::Loggable<Logger::Id::filter> {
public:
  // Network::ReadFilter
  SmtpFilter(SmtpFilterConfigSharedPtr config, TimeSource& time_source,
             Random::RandomGenerator& random_generator);
  ~SmtpFilter() {
    // delete session_;
    // session_ = nullptr;
    if (config_->accessLogs().size() > 0) {
      emitLogEntry(getStreamInfo());
    }
  }
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }
  Network::FilterStatus doDecode(Buffer::Instance& buffer, bool upstream);
  DecoderPtr createDecoder(DecoderCallbacks* callbacks, TimeSource& time_source,
                           Random::RandomGenerator&);

  const SmtpProxyStats& getStats() const { return config_->stats_; }

  Network::Connection& connection() const override { return read_callbacks_->connection(); }
  const SmtpFilterConfigSharedPtr& getConfig() const { return config_; }

  void incSmtpSessionRequests() override;
  void incSmtpConnectionEstablishmentErrors() override;
  void incTlsTerminatedSessions() override;
  void incSmtpTransactions() override;
  void incSmtpTransactionsAborted() override;
  void incSmtpSessionsCompleted() override;
  void incSmtpSessionsTerminated() override;
  void incTlsTerminationErrors() override;
  void incUpstreamTlsSuccess() override;
  void incUpstreamTlsFailed() override;

  void incSmtpAuthErrors() override;
  void incMailDataTransferErrors() override;
  void incMailRcptErrors() override;

  bool downstreamStartTls(absl::string_view response) override;
  bool sendReplyDownstream(absl::string_view response) override;
  bool upstreamTlsRequired() const override;
  bool tracingEnabled() override;
  bool sendUpstream(Buffer::Instance& buffer) override;

  bool upstreamStartTls() override;
  void closeDownstreamConnection() override;
  SmtpSession* getSession() { return decoder_->getSession(); }
  Buffer::OwnedImpl& getReadBuffer() override { return read_buffer_; }
  Buffer::OwnedImpl& getWriteBuffer() override { return write_buffer_; }
  void emitLogEntry(StreamInfo::StreamInfo& stream_info) override;
  StreamInfo::StreamInfo& getStreamInfo() override {
    return read_callbacks_->connection().streamInfo();
  }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};

  SmtpFilterConfigSharedPtr config_;
  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
  std::unique_ptr<Decoder> decoder_;
  TimeSource& time_source_;
  Random::RandomGenerator& random_generator_;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy