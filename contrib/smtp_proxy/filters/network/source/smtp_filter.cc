#include "contrib/smtp_proxy/filters/network/source/smtp_filter.h"
#include "envoy/network/connection.h"
#include "envoy/buffer/buffer.h"
#include <iostream>
#include <string>
#include "smtp_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

SmtpFilterConfig::SmtpFilterConfig(const SmtpFilterConfigOptions& config_options,
                                   Stats::Scope& scope)
    : scope_{scope}, stats_(generateStats(config_options.stats_prefix_, scope)) {
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

bool SmtpFilter::onStartTlsCommand(absl::string_view response) {

  Buffer::OwnedImpl buffer;
  buffer.add(response);

  read_callbacks_->connection().addBytesSentCallback([=](uint64_t bytes) -> bool {
    // Wait until 6 bytes long "220 OK" has been sent.
    if (bytes >= response.length()) {
      if (!read_callbacks_->connection().startSecureTransport()) {
        ENVOY_CONN_LOG(trace, "smtp_proxy filter: cannot switch to tls",
                       read_callbacks_->connection(), bytes);
      } else {
        // Switch to TLS has been completed.
        // Signal to the decoder to stop processing the current message (SSLRequest).
        // Because Envoy terminates SSL, the message was consumed and should not be
        // passed to other filters in the chain.
        incTlsTerminatedSessions();
        ENVOY_CONN_LOG(trace, "smtp_proxy filter: switched to tls", read_callbacks_->connection(),
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

void SmtpFilter::incTlsTerminatedSessions() { config_->stats_.tls_terminated_sessions_.inc(); }
void SmtpFilter::incSmtpTransactions() { config_->stats_.smtp_transactions_.inc(); }

void SmtpFilter::incSmtpTransactionsAborted() { config_->stats_.smtp_transactions_aborted_.inc(); }
void SmtpFilter::incSmtpSessionRequests() { config_->stats_.smtp_session_requests_.inc(); }
void SmtpFilter::incSmtpSessionsCompleted() { config_->stats_.smtp_sessions_completed_.inc(); }

void SmtpFilter::incSmtpConnectionEstablishmentErrors() {
  config_->stats_.smtp_connection_establishment_errors_.inc();
}

void SmtpFilter::incSmtp4xxErrors() { config_->stats_.smtp_errors_4xx_.inc(); }

void SmtpFilter::incSmtp5xxErrors() { config_->stats_.smtp_errors_5xx_.inc(); }

// onData method processes payloads sent by downstream client.
Network::FilterStatus SmtpFilter::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(trace, "smtp_proxy: got {} bytes", read_callbacks_->connection(), data.length(),
                 "end_stream ", end_stream);
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