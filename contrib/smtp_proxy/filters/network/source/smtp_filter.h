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
  COUNTER(sessions)                                                                            \
  COUNTER(sessions_bad_line)                                                                   \
  COUNTER(sessions_bad_pipeline)                                                               \
  COUNTER(sessions_bad_ehlo)                                                                   \
  COUNTER(sessions_non_esmtp)                                                                  \
  COUNTER(sessions_esmtp_unencrypted)                                                          \
  COUNTER(sessions_downstream_terminated_ssl)                                                  \
  COUNTER(sessions_upstream_terminated_ssl)                                                    \
  COUNTER(sessions_downstream_ssl_err)                                                         \
  COUNTER(sessions_upstream_ssl_err)                                                           \
  COUNTER(sessions_upstream_ssl_command_err)

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
    envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::SSLMode
        downstream_ssl_;
    envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::SSLMode
        upstream_ssl_;
  };
  SmtpFilterConfig(const SmtpFilterConfigOptions& config_options, Stats::Scope& scope);

  envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::SSLMode
      downstream_ssl_{
          envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::DISABLE};
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
		   Logger::Loggable<Logger::Id::filter> {
public:
  SmtpFilter(SmtpFilterConfigSharedPtr config);
  ~SmtpFilter() override = default;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override;

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

  bool onDownstreamSSLRequest();

  DecoderPtr createDecoder();
  void setDecoder(std::unique_ptr<Decoder> decoder) { decoder_ = std::move(decoder); }
  Decoder* getDecoder() const { return decoder_.get(); }

  // Routines used during integration and unit tests
  uint32_t getFrontendBufLength() const { return frontend_buffer_.length(); }
  uint32_t getBackendBufLength() const { return backend_buffer_.length(); }
  const SmtpProxyStats& getStats() const { return config_->stats_; }
  Network::Connection& connection() const { return read_callbacks_->connection(); }
  const SmtpFilterConfigSharedPtr& getConfig() const { return config_; }
  
private:
  enum State {
    PASSTHROUGH,
    UPSTREAM_GREETING,
    DOWNSTREAM_EHLO,
    UPSTREAM_EHLO_RESP,
    UPSTREAM_STARTTLS,
    UPSTREAM_STARTTLS_RESP,
    UPSTREAM_TLS_NEGO,
    DOWNSTREAM_STARTTLS,
    DOWNSTREAM_STARTTLS_RESP,
    DOWNSTREAM_TLS_NEGO,
  };

  State state_;

  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};
  SmtpFilterConfigSharedPtr config_;
  Buffer::OwnedImpl frontend_buffer_;
  Buffer::OwnedImpl backend_buffer_;
  Buffer::OwnedImpl downstream_esmtp_capabilities_;
  std::unique_ptr<Decoder> decoder_;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
