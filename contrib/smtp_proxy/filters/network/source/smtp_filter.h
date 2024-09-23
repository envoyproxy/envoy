#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/common/random_generator.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/common/common/utility.h"

#include "contrib/envoy/extensions/filters/network/smtp_proxy/v3alpha/smtp_proxy.pb.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_callbacks.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_decoder_impl.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_session.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_stats.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

class SmtpFilterConfig {
public:
  struct SmtpFilterConfigOptions {
    std::string stats_prefix_;
    bool tracing_;
    envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::SSLMode downstream_tls_;
    envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::SSLMode upstream_tls_;
    bool protocol_inspection_;
    std::vector<AccessLog::InstanceSharedPtr> access_logs_;
  };
  SmtpFilterConfig(const SmtpFilterConfigOptions& config_options, Stats::Scope& scope);
  const SmtpProxyStats& stats() { return stats_; }
  const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const { return access_logs_; }
  Stats::Scope& scope_;
  SmtpProxyStats stats_;
  bool tracing_{false};
  std::vector<AccessLog::InstanceSharedPtr> access_logs_;
  envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::SSLMode downstream_tls_{
      envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::DISABLE};
  envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::SSLMode upstream_tls_{
      envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::DISABLE};
  bool protocol_inspection_{false};

private:
  SmtpProxyStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return SmtpProxyStats{ALL_SMTP_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                               POOL_GAUGE_PREFIX(scope, prefix),
                                               POOL_HISTOGRAM_PREFIX(scope, prefix))};
  }
};

using SmtpFilterConfigSharedPtr = std::shared_ptr<SmtpFilterConfig>;

class SmtpFilter : public Network::Filter, DecoderCallbacks, Logger::Loggable<Logger::Id::filter> {
public:
  // Network::ReadFilter
  SmtpFilter(SmtpFilterConfigSharedPtr config, TimeSource& time_source,
             Random::RandomGenerator& random_generator);
  ~SmtpFilter() {
    // This will ensure session metadata is populated to stream_info and then logged before filter
    // chain is destroyed.
    // decoder_.reset();
    delete smtp_session_;
    smtp_session_ = nullptr;
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
  // Network::FilterStatus doDecode(Buffer::Instance& buffer, bool upstream);
  DecoderPtr createDecoder(DecoderCallbacks* callbacks, TimeSource& time_source,
                           Random::RandomGenerator&);

  SmtpProxyStats& getStats() override { return config_->stats_; }

  Network::Connection& connection() const override { return read_callbacks_->connection(); }
  const SmtpFilterConfigSharedPtr& getConfig() const { return config_; }

  void incSmtpSessionRequests() override;
  void incSmtpConnectionEstablishmentErrors() override;
  void incTlsTerminatedSessions() override;
  void incSmtpTransactionRequests() override;
  void incSmtpTransactionsCompleted() override;
  void incSmtpTransactionsAborted() override;
  void incSmtpTrxnFailed() override;
  void incSmtpSessionsCompleted() override;
  void incSmtpSessionsTerminated() override;
  void incTlsTerminationErrors() override;
  void incUpstreamTlsSuccess() override;
  void incUpstreamTlsFailed() override;
  void incActiveTransaction() override;
  void decActiveTransaction() override;
  void incActiveSession() override;
  void decActiveSession() override;

  void incSmtpAuthErrors() override;
  void incMailDataTransferErrors() override;
  void incMailRcptErrors() override;
  void inc4xxErrors() override;
  void inc5xxErrors() override;

  bool downstreamStartTls(absl::string_view response) override;
  bool sendReplyDownstream(absl::string_view response) override;
  bool upstreamTlsEnabled() const override;
  bool downstreamTlsEnabled() const override;
  bool downstreamTlsRequired() const override;
  bool upstreamTlsRequired() const override;
  bool protocolInspectionEnabled() const override;
  bool tracingEnabled() override;
  bool sendUpstream(Buffer::Instance& buffer) override;

  bool upstreamStartTls() override;
  void closeDownstreamConnection() override;
  SmtpSession* getSession() { return smtp_session_; }
  // SmtpSession* getSession() { return decoder_->getSession(); }
  Buffer::OwnedImpl& getReadBuffer() override { return read_buffer_; }
  Buffer::OwnedImpl& getWriteBuffer() override { return write_buffer_; }
  void emitLogEntry(StreamInfo::StreamInfo& stream_info) override;
  StreamInfo::StreamInfo& getStreamInfo() override {
    return read_callbacks_->connection().streamInfo();
  }
  void printSessionState(SmtpSession::State state);

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};

  SmtpFilterConfigSharedPtr config_;
  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
  std::unique_ptr<Decoder> decoder_;
  SmtpSession* smtp_session_;
  TimeSource& time_source_;
  Random::RandomGenerator& random_generator_;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
