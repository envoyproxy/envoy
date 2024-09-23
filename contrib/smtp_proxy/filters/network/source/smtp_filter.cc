#include "contrib/smtp_proxy/filters/network/source/smtp_filter.h"

#include <iostream>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

namespace {
const std::string known_commands[] = {"HELO", "EHLO", "AUTH", "MAIL",     "RCPT",
                                      "DATA", "QUIT", "RSET", "STARTTLS", "XREQID"};
}

SmtpFilterConfig::SmtpFilterConfig(const SmtpFilterConfigOptions& config_options,
                                   Stats::Scope& scope)
    : scope_{scope}, stats_(generateStats(config_options.stats_prefix_, scope)),
      tracing_(config_options.tracing_), downstream_tls_(config_options.downstream_tls_),
      upstream_tls_(config_options.upstream_tls_),
      protocol_inspection_(config_options.protocol_inspection_) {
  access_logs_ = config_options.access_logs_;
}

SmtpFilter::SmtpFilter(SmtpFilterConfigSharedPtr config, TimeSource& time_source,
                       Random::RandomGenerator& random_generator)
    : config_{config}, time_source_(time_source), random_generator_(random_generator) {}

void SmtpFilter::emitLogEntry(StreamInfo::StreamInfo& stream_info) {
  for (const auto& access_log : config_->accessLogs()) {
    access_log->log({}, stream_info);
  }
}

Network::FilterStatus SmtpFilter::onNewConnection() {
  incSmtpSessionRequests();
  if (!decoder_) {
    decoder_ = createDecoder(this, time_source_, random_generator_);
  }
  return Network::FilterStatus::Continue;
}

bool SmtpFilter::downstreamStartTls(absl::string_view response) {
  Buffer::OwnedImpl buffer;
  buffer.add(response);

  read_callbacks_->connection().addBytesSentCallback([=](uint64_t bytes) -> bool {
    // Wait until response has been sent.
    if (bytes >= response.length()) {
      if (!read_callbacks_->connection().startSecureTransport()) {
        ENVOY_CONN_LOG(trace, "smtp_proxy filter: cannot switch to tls",
                       read_callbacks_->connection(), bytes);
        return true;
      } else {
        // Switch to TLS has been completed.
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

bool SmtpFilter::sendUpstream(Buffer::Instance& data) {
  read_callbacks_->injectReadDataToFilterChain(data, false);
  return true;
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

bool SmtpFilter::upstreamTlsEnabled() const {
  return (config_->upstream_tls_ ==
              envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::ENABLE ||
          config_->upstream_tls_ ==
              envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::REQUIRE);
}

bool SmtpFilter::downstreamTlsEnabled() const {
  return (config_->downstream_tls_ ==
              envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::ENABLE ||
          config_->downstream_tls_ ==
              envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::REQUIRE);
}

bool SmtpFilter::downstreamTlsRequired() const {
  return (config_->downstream_tls_ ==
          envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::REQUIRE);
}

bool SmtpFilter::upstreamTlsRequired() const {
  return (config_->downstream_tls_ ==
          envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::REQUIRE);
}

bool SmtpFilter::protocolInspectionEnabled() const { return config_->protocol_inspection_; }

bool SmtpFilter::tracingEnabled() { return config_->tracing_; }

bool SmtpFilter::upstreamStartTls() {
  // Try to switch upstream connection to use a secure channel.
  if (read_callbacks_->startUpstreamSecureTransport()) {
    config_->stats_.upstream_tls_success_.inc();
    ENVOY_CONN_LOG(trace, "smtp_proxy: upstream TLS enabled.", read_callbacks_->connection());
  } else {
    ENVOY_CONN_LOG(info,
                   "smtp_proxy: cannot enable upstream secure transport. Check "
                   "configuration. Terminating.",
                   read_callbacks_->connection());
    config_->stats_.upstream_tls_error_.inc();
    return false;
  }
  return true;
}

void SmtpFilter::incSmtpTransactionRequests() { config_->stats_.transaction_req_.inc(); }
void SmtpFilter::incSmtpTransactionsCompleted() { config_->stats_.transaction_completed_.inc(); }

void SmtpFilter::incSmtpTransactionsAborted() { config_->stats_.transaction_aborted_.inc(); }
void SmtpFilter::incSmtpSessionRequests() { config_->stats_.session_requests_.inc(); }
void SmtpFilter::incSmtpSessionsCompleted() { config_->stats_.session_completed_.inc(); }
void SmtpFilter::incActiveTransaction() { config_->stats_.transaction_active_.inc(); }
void SmtpFilter::decActiveTransaction() { config_->stats_.transaction_active_.dec(); }
void SmtpFilter::incActiveSession() { config_->stats_.session_active_.inc(); }
void SmtpFilter::decActiveSession() { config_->stats_.session_active_.dec(); }

void SmtpFilter::incSmtpSessionsTerminated() { config_->stats_.session_terminated_.inc(); }

void SmtpFilter::incSmtpConnectionEstablishmentErrors() {
  config_->stats_.connection_establishment_errors_.inc();
}

void SmtpFilter::incMailDataTransferErrors() { config_->stats_.mail_data_transfer_errors_.inc(); }

void SmtpFilter::incSmtpTrxnFailed() { config_->stats_.transaction_failed_.inc(); }

void SmtpFilter::incMailRcptErrors() { config_->stats_.rcpt_errors_.inc(); }
void SmtpFilter::inc4xxErrors() { config_->stats_.upstream_4xx_errors_.inc(); }
void SmtpFilter::inc5xxErrors() { config_->stats_.upstream_5xx_errors_.inc(); }

void SmtpFilter::incTlsTerminatedSessions() {
  config_->stats_.downstream_tls_termination_success_.inc();
}

void SmtpFilter::incTlsTerminationErrors() {
  config_->stats_.downstream_tls_termination_error_.inc();
}

void SmtpFilter::incSmtpAuthErrors() { config_->stats_.auth_errors_.inc(); }

void SmtpFilter::incUpstreamTlsSuccess() { config_->stats_.upstream_tls_success_.inc(); }

void SmtpFilter::incUpstreamTlsFailed() { config_->stats_.upstream_tls_error_.inc(); }

void SmtpFilter::closeDownstreamConnection() {
  read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

// onData method processes payloads sent by downstream client.
Network::FilterStatus SmtpFilter::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(debug, "smtp_proxy: got {} bytes", read_callbacks_->connection(), data.length(),
                 "end_stream ", end_stream);
  ENVOY_LOG(debug, "smtp_proxy: received command {}", data.toString());

  // printSessionState(getSession()->getState());
  if (getSession()->getState() == SmtpSession::State::Passthrough || getSession()->isTerminated()) {
    return Network::FilterStatus::Continue;
  }
  // Do not parse payload when DATA transfer is taking place.
  if (getSession()->isDataTransferInProgress()) {
    getSession()->updateBytesMeterOnCommand(data);
    return Network::FilterStatus::Continue;
  }
  read_buffer_.add(data);
  Decoder::Command command;
  SmtpUtils::Result result = decoder_->parseCommand(read_buffer_, command);
  switch (result) {
  case SmtpUtils::Result::ProtocolError: {
    read_buffer_.drain(read_buffer_.length());
    getStats().passthrough_sessions_.inc();
    getSession()->setState(SmtpSession::State::Passthrough);
    getSession()->setStatus(SmtpUtils::protocol_error);
    return Network::FilterStatus::Continue;
  }
  case SmtpUtils::Result::NeedMoreData:
    return Network::FilterStatus::Continue;
  case SmtpUtils::Result::ReadyForNext:
    break;
  default:
    break;
  }

  for (const auto& c : known_commands) {
    if (absl::AsciiStrToUpper(command.verb) != c) {
      continue;
    }
    result = smtp_session_->handleCommand(command.verb, command.args);
    break;
  }

  switch (result) {
  case SmtpUtils::Result::ProtocolError:
    getStats().passthrough_sessions_.inc();
    getSession()->setState(SmtpSession::State::Passthrough);
    getSession()->setStatus(SmtpUtils::protocol_error);
    return Network::FilterStatus::Continue;
  case SmtpUtils::Result::ReadyForNext:
    break;
  case SmtpUtils::Result::Stopped: {
    ASSERT(read_buffer_.length() == 0);
    data.drain(data.length());
    return Network::FilterStatus::StopIteration;
  }
  default:
    break;
  }
  return Network::FilterStatus::Continue;
}

void SmtpFilter::printSessionState(SmtpSession::State state) {

  switch (state) {
  case SmtpSession::State::Passthrough:
    std::cout << " current session state: Passthrough" << std::endl;
    break;
  case SmtpSession::State::SessionInProgress:
    std::cout << " current session state: SessionInProgress" << std::endl;
    break;
  case SmtpSession::State::SessionTerminated:
    std::cout << " current session state: SessionTerminated" << std::endl;
    break;
  case SmtpSession::State::SessionAuthRequest:
    std::cout << " current session state: SessionAuthRequest" << std::endl;
    break;
  case SmtpSession::State::SessionInitRequest:
    std::cout << " current session state: SessionInitRequest" << std::endl;
    break;
  case SmtpSession::State::XReqIdTransfer:
    std::cout << " current session state: XReqIdTransfer" << std::endl;
    break;
  case SmtpSession::State::SessionResetRequest:
    std::cout << " current session state: SessionResetRequest" << std::endl;
    break;
  case SmtpSession::State::SessionTerminationRequest:
    std::cout << " current session state: SessionTerminationRequest" << std::endl;
    break;
  case SmtpSession::State::ConnectionSuccess:
    std::cout << " current session state: ConnectionSuccess" << std::endl;
    break;
  default:
    break;
  }
}

// onWrite method processes payloads sent by upstream to the client.
Network::FilterStatus SmtpFilter::onWrite(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(debug, "smtp_proxy: got {} bytes", write_callbacks_->connection(), data.length(),
                 "end_stream ", end_stream);
  // ENVOY_LOG(debug, "smtp_proxy: received response {}", data.toString());
  // printSessionState(getSession()->getState());
  if (getSession()->getState() == SmtpSession::State::Passthrough || getSession()->isTerminated()) {
    return Network::FilterStatus::Continue;
  }
  write_buffer_.add(data);

  Decoder::Response response;
  SmtpUtils::Result result = decoder_->parseResponse(write_buffer_, response);
  ENVOY_LOG(debug, "smtp_proxy: response code {}", response.resp_code);
  ENVOY_LOG(debug, "smtp_proxy: response msg {}", response.msg);
  ENVOY_LOG(debug, "smtp_proxy: response len {}", response.len);

  // if(response.len != data.length()) {
  //   result = SmtpUtils::Result::ProtocolError;
  // }

  switch (result) {
  case SmtpUtils::Result::ProtocolError: {
    write_buffer_.drain(write_buffer_.length());
    getSession()->setState(SmtpSession::State::Passthrough);
    getSession()->setStatus(SmtpUtils::protocol_error);
    getStats().passthrough_sessions_.inc();
    return Network::FilterStatus::Continue;
  }
  case SmtpUtils::Result::NeedMoreData: {
    return Network::FilterStatus::Continue;
  }
  case SmtpUtils::Result::ReadyForNext:
    break;
  default:
    break;
  }
  // if (response.len != write_buffer_.length()) {
  //   write_buffer_.drain(write_buffer_.length());
  //   return Network::FilterStatus::Continue;
  // }
  result = smtp_session_->handleResponse(response.resp_code, response.msg);

  switch (result) {
  case SmtpUtils::Result::ProtocolError:
    getSession()->setStatus(SmtpUtils::protocol_error);
    getSession()->setState(SmtpSession::State::Passthrough);
    getStats().passthrough_sessions_.inc();
    return Network::FilterStatus::Continue;
  case SmtpUtils::Result::ReadyForNext:
    break;
  case SmtpUtils::Result::Stopped: {
    ASSERT(write_buffer_.length() == 0);
    data.drain(data.length());
    return Network::FilterStatus::StopIteration;
  }
  default:
    break;
  }
  return Network::FilterStatus::Continue;
}

// Network::FilterStatus SmtpFilter::doDecode(Buffer::Instance& data, bool upstream) {

//   switch (decoder_->onData(data, upstream)) {
//   case SmtpUtils::Result::NeedMoreData:
//     return Network::FilterStatus::Continue;
//   case SmtpUtils::Result::ReadyForNext:
//     return Network::FilterStatus::Continue;
//   case SmtpUtils::Result::Stopped:
//     return Network::FilterStatus::StopIteration;
//   default:
//     break;
//   }
//   return Network::FilterStatus::Continue;
// }

DecoderPtr SmtpFilter::createDecoder(DecoderCallbacks* callbacks, TimeSource& time_source,
                                     Random::RandomGenerator& random_generator) {
  smtp_session_ = new SmtpSession(callbacks, time_source, random_generator);
  return std::make_unique<DecoderImpl>();
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
