#include "contrib/smtp_proxy/filters/network/source/smtp_filter.h"
#include "envoy/network/connection.h"
#include "envoy/buffer/buffer.h"
#include "source/common/common/assert.h"

#include <iostream>
#include <string>
#include "smtp_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

SmtpFilterConfig::SmtpFilterConfig(const SmtpFilterConfigOptions& config_options,
                                   Stats::Scope& scope)
    : scope_{scope}, stats_(generateStats(config_options.stats_prefix_, scope)),
      upstream_tls_(config_options.upstream_tls_) {
  std::cout << config_options.stats_prefix_ << "\n";
}
SmtpFilter::SmtpFilter(SmtpFilterConfigSharedPtr config) : config_{config} {
  if (!decoder_) {
    decoder_ = createDecoder(this);
  }
}

Network::FilterStatus SmtpFilter::onNewConnection() {
  incSmtpSessionRequests();
  return Network::FilterStatus::Continue;
}

bool SmtpFilter::downstreamStartTls(absl::string_view response) {

  Buffer::OwnedImpl buffer;
  buffer.add(response);

  read_callbacks_->connection().addBytesSentCallback([=](uint64_t bytes) -> bool {
    // Wait until response has been sent.
    if (bytes >= response.length()) {
      if (!read_callbacks_->connection().startSecureTransport()) {
        ENVOY_CONN_LOG(debug, "smtp_proxy filter: cannot switch to tls",
                       read_callbacks_->connection(), bytes);
      } else {
        // Switch to TLS has been completed.
        incTlsTerminatedSessions();
        ENVOY_CONN_LOG(debug, "smtp_proxy filter: switched to tls", read_callbacks_->connection(),
                       bytes);
        return false;
      }
    }
    return true;
  });

  read_callbacks_->connection().write(buffer, false);
  return false;
}

bool SmtpFilter::sendReplyDownstream(absl::string_view response) {
  Buffer::OwnedImpl buffer;
  buffer.add(response);
  if (read_callbacks_->connection().state() != Network::Connection::State::Open) {
    ENVOY_LOG(warn, "downstream connection is closed or closing");
    return true;
  }
  read_callbacks_->connection().addBytesSentCallback([=](uint64_t bytes) -> bool {
    // Wait until response has been sent.
    if (bytes >= response.length()) {
      return false;
    }
    return true;
  });

  read_callbacks_->connection().write(buffer, false);
  return false;
}

bool SmtpFilter::upstreamTlsRequired() const {
  return (config_->upstream_tls_ ==
          envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::REQUIRE);
}


bool SmtpFilter::upstreamStartTls() {
  // Try to switch upstream connection to use a secure channel.
  if (read_callbacks_->startUpstreamSecureTransport()) {
    config_->stats_.sessions_upstream_tls_success_.inc();
    // read_callbacks_->injectReadDataToFilterChain(data, false);
    ENVOY_LOG(debug, "smtp_proxy: upstream TLS enabled");
    ENVOY_CONN_LOG(trace, "smtp_proxy: upstream TLS enabled.", read_callbacks_->connection());
  } else {
    ENVOY_CONN_LOG(info,
                   "smtp_proxy: cannot enable upstream secure transport. Check "
                   "configuration. Terminating.",
                   read_callbacks_->connection());
    ENVOY_LOG(debug, "smtp_proxy: cannot enable upstream secure transport. Check "
                     "configuration. Terminating.");
    config_->stats_.sessions_upstream_tls_failed_.inc();
    return false;
  }
  return true;
}

void SmtpFilter::incSmtpTransactions() { config_->stats_.smtp_transactions_.inc(); }

void SmtpFilter::incSmtpTransactionsAborted() { config_->stats_.smtp_transactions_aborted_.inc(); }
void SmtpFilter::incSmtpSessionRequests() { config_->stats_.smtp_session_requests_.inc(); }
void SmtpFilter::incSmtpSessionsCompleted() { config_->stats_.smtp_sessions_completed_.inc(); }

void SmtpFilter::incSmtpSessionsTerminated() { config_->stats_.smtp_sessions_terminated_.inc(); }

void SmtpFilter::incSmtpConnectionEstablishmentErrors() {
  config_->stats_.smtp_connection_establishment_errors_.inc();
}

void SmtpFilter::incMailDataTransferErrors() { config_->stats_.smtp_mail_data_transfer_errors_.inc(); }
void SmtpFilter::incMailRcptErrors() { config_->stats_.smtp_rcpt_errors_.inc(); }

void SmtpFilter::incTlsTerminatedSessions() { config_->stats_.smtp_tls_terminated_sessions_.inc(); }

void SmtpFilter::incTlsTerminationErrors() { config_->stats_.smtp_tls_termination_errors_.inc(); }

void SmtpFilter::incSmtpAuthErrors() { config_->stats_.smtp_auth_errors_.inc(); }

void SmtpFilter::incUpstreamTlsSuccess() { config_->stats_.sessions_upstream_tls_success_.inc(); }

void SmtpFilter::incUpstreamTlsFailed() { config_->stats_.sessions_upstream_tls_failed_.inc(); }

void SmtpFilter::closeDownstreamConnection() {
  read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
  incSmtpSessionsTerminated();
}

// onData method processes payloads sent by downstream client.
Network::FilterStatus SmtpFilter::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(trace, "smtp_proxy: got {} bytes", read_callbacks_->connection(), data.length(),
                 "end_stream ", end_stream);
  ENVOY_LOG(debug, "smtp_proxy onRead received data: ", StringUtil::trim(data.toString()));
  read_buffer_.add(data);
  Network::FilterStatus result = doDecode(read_buffer_, false);
  if (result == Network::FilterStatus::StopIteration) {
    ASSERT(read_buffer_.length() == 0);
    data.drain(data.length());
  }
  return result;
}

// onWrite method processes payloads sent by upstream to the client.
Network::FilterStatus SmtpFilter::onWrite(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(trace, "smtp_proxy: got {} bytes", write_callbacks_->connection(), data.length(),
                 "end_stream ", end_stream);
  ENVOY_LOG(debug, "smtp_proxy onWrite received data: ", StringUtil::trim(data.toString()));
  write_buffer_.add(data);
  Network::FilterStatus result = doDecode(write_buffer_, true);
  if (result == Network::FilterStatus::StopIteration) {
    ASSERT(write_buffer_.length() == 0);
    data.drain(data.length());
  }
  return result;
}

Network::FilterStatus SmtpFilter::doDecode(Buffer::Instance& data, bool upstream) {

  switch (decoder_->onData(data, upstream)) {
  case Decoder::Result::ReadyForNext:
    return Network::FilterStatus::Continue;
  case Decoder::Result::Stopped:
    return Network::FilterStatus::StopIteration;
  }
  return Network::FilterStatus::Continue;
}

DecoderPtr SmtpFilter::createDecoder(DecoderCallbacks* callbacks) {
  return std::make_unique<DecoderImpl>(callbacks);
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy