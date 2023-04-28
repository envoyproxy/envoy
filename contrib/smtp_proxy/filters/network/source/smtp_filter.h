#pragma once

#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "envoy/config/core/v3/proxy_protocol.pb.h"

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
  COUNTER(sessions_terminated_ssl)                                                             \
  COUNTER(sessions_no_ehlo_after_starttls)


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
    absl::optional<envoy::config::core::v3::ProxyProtocolConfig> proxy_protocol_config_;
    bool terminate_ssl_;
    envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::SSLMode
        upstream_ssl_;
  };
  SmtpFilterConfig(const SmtpFilterConfigOptions& config_options, Stats::Scope& scope);

  absl::optional<envoy::config::core::v3::ProxyProtocolConfig> proxy_protocol_config_;
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

  bool onSSLRequest();

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

    // read first command from client
    // if syntax error or not helo or ehlo
    //   close connection
    // if helo
    //   leave command in frontend_buffer_
    //   send proxy header to upstream if configured
    //   -> UPSTREAM_RESPONSE
    // if ehlo
    //   send configured esmtp capabilities to client
    //   -> STARTTLS
    EHLO,

    // read next command from client
    // if not STARTTLS
    //   leave command in frontend_buffer_
    //   send proxy header to upstream if configured
    //   -> UPSTREAM_RESPONSE
    // if STARTTLS
    //   send response
    //   enable tls on transport socket after response put on wire
    //   -> EHLO2
    STARTTLS,

    // consume the banner from the upstream
    // -> EHLO2
    UPSTREAM_BANNER,

    // read first command from the client after STARTTLS
    // if ehlo
    //   -> PASSTHROUGH
    // else
    //   buffer command
    //   emit ehlo to upstream
    //   -> UPSTREAM_RESPONSE
    EHLO2,

    // consume/discard one smtp response from the upstream,
    // send command in frontend_buffer_ to upstream,
    // -> PASSTHROUGH
    UPSTREAM_RESPONSE,
  };

  // The filter callbacks may have previously buffered data in
  // frontend/backend_buffer_ while we were talking to the opposite
  // side that we're ready to process now.
  Network::FilterStatus readUpstream(State new_state);
  Network::FilterStatus readDownstream(State new_state);

  Network::FilterStatus maybeSendProxyHeader(State next);


  State state_;

  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};
  SmtpFilterConfigSharedPtr config_;
  Buffer::OwnedImpl frontend_buffer_;
  Buffer::OwnedImpl backend_buffer_;
  std::unique_ptr<Decoder> decoder_;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
