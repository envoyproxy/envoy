#include "contrib/smtp_proxy/filters/network/source/smtp_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"

using ConfigProto = envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

SmtpFilterConfig::SmtpFilterConfig(const SmtpFilterConfigOptions& config_options,
                                   Stats::Scope& scope)
    : downstream_ssl_(config_options.downstream_ssl_), upstream_ssl_(config_options.upstream_ssl_),
      scope_{scope}, stats_{generateStats(config_options.stats_prefix_, scope)} {}

SmtpFilter::SmtpFilter(SmtpFilterConfigSharedPtr config) : config_{config} {
  if (!decoder_) {
    decoder_ = createDecoder();
  }
}

Network::FilterStatus SmtpFilter::onNewConnection() {
  ENVOY_CONN_LOG(trace, "smtp_proxy: onNewConnection()", read_callbacks_->connection());

  config_->stats_.sessions_.inc();

  state_ = UPSTREAM_GREETING;

  return Network::FilterStatus::Continue;
}

// Network::ReadFilter
Network::FilterStatus SmtpFilter::onData(Buffer::Instance& data, bool) {
  ENVOY_CONN_LOG(trace, "smtp_proxy: onData {} bytes, state {}", read_callbacks_->connection(),
                 data.length(), state_);

  switch (state_) {
  case PASSTHROUGH:
    return Network::FilterStatus::Continue;
  case DOWNSTREAM_EHLO:
  case DOWNSTREAM_STARTTLS:
    break;
  default:
    // client spoke out of turn -> hang up
    config_->stats_.sessions_downstream_desync_.inc();
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return Network::FilterStatus::StopIteration;
  }

  frontend_buffer_.add(data);
  data.drain(data.length());

  Decoder::Command command;

  Decoder::Result res = decoder_->DecodeCommand(frontend_buffer_, command);

  switch (res) {
  case Decoder::Result::Bad: {
    config_->stats_.sessions_bad_line_.inc();
    Buffer::OwnedImpl err("500 syntax error\r\n");
    write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
    // TODO may not always want to hang up here though the decoder
    // isn't very picky and only enforces an upper bound on max line
    // length. This will break if the server advertises an extension
    // that allows a huge command line and the command immediately
    // after EHLO is that but that seems unlikely.
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return Network::FilterStatus::StopIteration;
  }
  case Decoder::Result::NeedMoreData:
    return Network::FilterStatus::Continue;
  case Decoder::Result::ReadyForNext:
    break;
  }

  if (command.wire_len != frontend_buffer_.length()) {
    config_->stats_.sessions_bad_pipeline_.inc();
    ENVOY_CONN_LOG(trace, "smtp_proxy: bad pipeline {} {}", read_callbacks_->connection(),
                   command.wire_len, frontend_buffer_.length());

    Buffer::OwnedImpl err("503 bad pipeline\r\n");
    write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return Network::FilterStatus::StopIteration;
  }

  ENVOY_CONN_LOG(trace, "smtp_proxy: state {} command {}", read_callbacks_->connection(), state_,
                 command.verb);

  Buffer::OwnedImpl wire_command;
  wire_command.move(frontend_buffer_, command.wire_len);

  switch (state_) {
  case DOWNSTREAM_EHLO: {
    if (command.verb != Decoder::Command::HELO && command.verb != Decoder::Command::EHLO) {
      config_->stats_.sessions_bad_ehlo_.inc();
      Buffer::OwnedImpl err("503 bad sequence of commands\r\n");
      write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
      read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      return Network::FilterStatus::StopIteration;
    }
    read_callbacks_->injectReadDataToFilterChain(wire_command, /*end_stream=*/false);
    if (command.verb == Decoder::Command::HELO) {
      config_->stats_.sessions_non_esmtp_.inc();
      state_ = PASSTHROUGH;
      return Network::FilterStatus::StopIteration;
    }
    state_ = UPSTREAM_EHLO_RESP;
    return Network::FilterStatus::StopIteration;
  }
  case DOWNSTREAM_STARTTLS: {
    if (command.verb != Decoder::Command::STARTTLS) {
      if (config_->downstream_ssl_ == ConfigProto::REQUIRE) {
        Buffer::OwnedImpl err("530 TLS required\r\n"); // rfc3207
        write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
        read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
        return Network::FilterStatus::StopIteration;
      }

      // If we wanted to send XCLIENT to the upstream indicating that
      // the client is not TLS, we would start that here.
      read_callbacks_->injectReadDataToFilterChain(wire_command, /*end_stream=*/false);
      config_->stats_.sessions_esmtp_unencrypted_.inc();
      state_ = PASSTHROUGH;
      return Network::FilterStatus::StopIteration;
    }
    onDownstreamSSLRequest();
    return Network::FilterStatus::StopIteration;
  }
  default:
    ASSERT(0); // ABSL_UNREACHABLE();
  }
  ASSERT(0); // ABSL_UNREACHABLE();
}

void SmtpFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  ENVOY_CONN_LOG(trace, "smtp_proxy: initializeReadFilterCallbacks()", callbacks.connection());
  read_callbacks_ = &callbacks;
}

void SmtpFilter::initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) {
  ENVOY_CONN_LOG(trace, "smtp_proxy: initializeWriteFilterCallbacks()", callbacks.connection());
  write_callbacks_ = &callbacks;
}

// Network::WriteFilter
Network::FilterStatus SmtpFilter::onWrite(Buffer::Instance& data, bool) {
  ENVOY_CONN_LOG(trace, "smtp_proxy: onWrite {} bytes, state {}", read_callbacks_->connection(),
                 data.length(), state_);

  switch (state_) {
  case PASSTHROUGH:
    return Network::FilterStatus::Continue;
  case UPSTREAM_GREETING:
  case UPSTREAM_EHLO_RESP:
  case UPSTREAM_STARTTLS_RESP:
    break;
  default:
    // upstream spoke out of turn -> hang up
    config_->stats_.sessions_upstream_desync_.inc();
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return Network::FilterStatus::StopIteration;
  }

  backend_buffer_.add(data);
  data.drain(data.length());

  // We need to decode the response to know that we consumed it all
  Decoder::Response response;
  Decoder::Result res = decoder_->DecodeResponse(backend_buffer_, response);
  switch (res) {
  case Decoder::Result::Bad: {
    Buffer::OwnedImpl err("451 upstream syntax error\r\n");
    write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return Network::FilterStatus::StopIteration;
  }
  case Decoder::Result::NeedMoreData:
    return Network::FilterStatus::Continue;
  case Decoder::Result::ReadyForNext:
    break;
  }

  if (response.wire_len != backend_buffer_.length()) {
    Buffer::OwnedImpl err("451 upstream bad pipeline\r\n");
    write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return Network::FilterStatus::StopIteration;
  }

  Buffer::OwnedImpl wire_resp;
  wire_resp.move(backend_buffer_, response.wire_len);

  switch (state_) {
  case UPSTREAM_GREETING: {
    write_callbacks_->injectWriteDataToFilterChain(wire_resp, /*end_stream=*/false);
    state_ = DOWNSTREAM_EHLO;
    return Network::FilterStatus::StopIteration;
  }
  case UPSTREAM_EHLO_RESP: {
    if (config_->upstream_ssl_ == ConfigProto::REQUIRE &&
        !decoder_->HasEsmtpCapability("STARTTLS", wire_resp.toString())) {
      Buffer::OwnedImpl err("400 upstream STARTTLS not offered\r\n");
      write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
      read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      return Network::FilterStatus::StopIteration;
    }

    {
      std::string downstream_caps = wire_resp.toString();
      if (config_->downstream_ssl_ >= ConfigProto::ENABLE) {
        decoder_->AddEsmtpCapability("STARTTLS", downstream_caps);
      } else {
        decoder_->RemoveEsmtpCapability("STARTTLS", downstream_caps);
      }
      downstream_esmtp_capabilities_ = Buffer::OwnedImpl(downstream_caps);
    }

    if (config_->upstream_ssl_ >= ConfigProto::ENABLE) {
      state_ = UPSTREAM_STARTTLS_RESP;
      Buffer::OwnedImpl starttls("STARTTLS\r\n");
      read_callbacks_->injectReadDataToFilterChain(starttls, /*end_stream=*/false);
    } else {
      write_callbacks_->injectWriteDataToFilterChain(downstream_esmtp_capabilities_,
                                                     /*end_stream=*/false);
      state_ = DOWNSTREAM_STARTTLS;
    }
    return Network::FilterStatus::StopIteration;
  }
  case UPSTREAM_STARTTLS_RESP: {
    // this seems unlikely and is probably indicative of problems with
    // the upstream, just fail for now
    if ((response.code < 200) || (response.code > 299)) {
      config_->stats_.sessions_upstream_ssl_command_err_.inc();
      Buffer::OwnedImpl err("400 upstream STARTTLS error\r\n");
      write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
      read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    } else if (!read_callbacks_->startUpstreamSecureTransport()) {
      config_->stats_.sessions_upstream_ssl_err_.inc();
      ENVOY_CONN_LOG(info,
                     "smtp_proxy: cannot enable upstream secure transport. "
                     "Check configuration.",
                     read_callbacks_->connection());
      Buffer::OwnedImpl err("400 upstream TLS negotiation failure\r\n");
      write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
      read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    } else {
      config_->stats_.sessions_upstream_terminated_ssl_.inc();

      write_callbacks_->injectWriteDataToFilterChain(downstream_esmtp_capabilities_,
                                                     /*end_stream=*/false);
      if (config_->downstream_ssl_ >= ConfigProto::ENABLE) {
        state_ = DOWNSTREAM_STARTTLS;
      } else {
        state_ = PASSTHROUGH;
      }
    }
    return Network::FilterStatus::StopIteration;
  }
  default:
    ASSERT(0); // ABSL_UNREACHABLE();
  }
  ASSERT(0); // ABSL_UNREACHABLE();
}

DecoderPtr SmtpFilter::createDecoder() { return std::make_unique<DecoderImpl>(); }

bool SmtpFilter::onDownstreamSSLRequest() {
  ENVOY_CONN_LOG(trace, "smtp_proxy: onDownstreamSSLRequest()", read_callbacks_->connection());

  Buffer::OwnedImpl buf("220 envoy ready for tls\r\n");
  size_t len = buf.length();
  // Add callback to be notified when the reply message has been
  // transmitted.
  read_callbacks_->connection().addBytesSentCallback([=](uint64_t bytes) -> bool {
    // Wait until response has been transmitted.
    if (bytes >= len) {
      if (!read_callbacks_->connection().startSecureTransport()) {
        config_->stats_.sessions_downstream_ssl_err_.inc();

        ENVOY_CONN_LOG(
            info, "smtp_proxy: cannot enable downstream secure transport. Check configuration.",
            read_callbacks_->connection());
        read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
      } else {
        // Unsubscribe the callback.
        config_->stats_.sessions_downstream_terminated_ssl_.inc();
        ENVOY_CONN_LOG(trace, "smtp_proxy: enabled SSL termination.",
                       read_callbacks_->connection());

        // if we wanted to send XCLIENT to the upstream with the TLS
        // info, we would start that here.
        state_ = PASSTHROUGH;

        // Switch to TLS has been completed.
        // Signal to the decoder to stop processing the current message (SSLRequest).
        // Because Envoy terminates SSL, the message was consumed and should not be
        // passed to other filters in the chain.
        return false;
      }
    }
    return true;
  });
  write_callbacks_->injectWriteDataToFilterChain(buf, false);

  return false;
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
