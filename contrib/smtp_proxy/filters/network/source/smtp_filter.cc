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
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
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
    // TODO(jsbucy): may not always want to hang up here though the decoder
    // isn't very picky and only enforces an upper bound on max line
    // length. This will break if the server advertises an extension
    // that allows a huge command line and the command immediately
    // after EHLO is that but that seems unlikely.
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
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
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
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
      read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
      return Network::FilterStatus::StopIteration;
    }
    downstream_ehlo_command_.add(wire_command);
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
        read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
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
    ABSL_UNREACHABLE();
  }
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
  case UPSTREAM_EHLO2_RESP:
    break;
  default:
    // upstream spoke out of turn -> hang up
    config_->stats_.sessions_upstream_desync_.inc();
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
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
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
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
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
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
    // If we're doing upstream starttls, don't send the ehlo resp to
    // the client yet so we can report an upstream starttls failure or
    // get the post-tls capabilities: UPSTREAM_STARTTLS_RESP (below).
    if (config_->upstream_ssl_ == ConfigProto::REQUIRE &&
        !decoder_->HasEsmtpCapability("STARTTLS", wire_resp.toString())) {
      Buffer::OwnedImpl err("400 upstream STARTTLS not offered\r\n");
      write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
      read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
      return Network::FilterStatus::StopIteration;
    }
    if (config_->upstream_ssl_ >= ConfigProto::ENABLE &&
        decoder_->HasEsmtpCapability("STARTTLS", wire_resp.toString())) {
      state_ = UPSTREAM_STARTTLS_RESP;
      Buffer::OwnedImpl starttls("STARTTLS\r\n");
      read_callbacks_->injectReadDataToFilterChain(starttls, /*end_stream=*/false);
      return Network::FilterStatus::StopIteration;
    }
    if (config_->downstream_ssl_ == ConfigProto::DISABLE) {
      // and upstream either was disabled or not offered
      write_callbacks_->injectWriteDataToFilterChain(wire_resp, /*end_stream=*/false);
      config_->stats_.sessions_esmtp_unencrypted_.inc();
      state_ = PASSTHROUGH;
      return Network::FilterStatus::StopIteration;
    }
    ABSL_FALLTHROUGH_INTENDED;
  }
  case UPSTREAM_EHLO2_RESP: {
    // If upstream and downstream ssl termination are disabled, there
    // is nothing to prevent the server from advertising and the
    // client invoking STARTTLS. If someone wanted to prevent that, we
    // could add an additional config knob to strip the STARTTLS
    // capability but that seems like an unlikely configuration?
    ASSERT(config_->downstream_ssl_ >= ConfigProto::ENABLE);
    Buffer::OwnedImpl downstream_esmtp_capabilities;
    {
      std::string downstream_caps = wire_resp.toString();
      decoder_->AddEsmtpCapability("STARTTLS", downstream_caps);
      downstream_esmtp_capabilities.add(downstream_caps);
    }
    write_callbacks_->injectWriteDataToFilterChain(downstream_esmtp_capabilities,
                                                   /*end_stream=*/false);
    state_ = DOWNSTREAM_STARTTLS;
    return Network::FilterStatus::StopIteration;
  }

  case UPSTREAM_STARTTLS_RESP: {
    if ((response.code < 200) || (response.code > 299)) {
      // This seems unlikely and is probably indicative of problems
      // with the upstream, just fail for now.
      config_->stats_.sessions_upstream_ssl_command_err_.inc();
      Buffer::OwnedImpl err("400 upstream STARTTLS error\r\n");
      write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
      read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
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

      // The downstream smtp client is still waiting for the EHLO
      // response, resend their original EHLO to the upstream since
      // the server may return different capabilities now that we've
      // upgraded to TLS.

      read_callbacks_->injectReadDataToFilterChain(downstream_ehlo_command_,
                                                   /*end_stream=*/false);
      if (config_->downstream_ssl_ >= ConfigProto::ENABLE) {
        state_ = UPSTREAM_EHLO2_RESP;
      } else {
        // If there was a buggy smtp server that continued to
        // advertise STARTTLS after it had already done it once, that
        // would probably cause problems here since we'll pass it
        // through to the client which could try to invoke it. If we
        // wanted to try to paper over it here, we would do
        // UPSTREAM_EHLO2_RESP and strip the capability there.
        state_ = PASSTHROUGH;
      }
    }
    return Network::FilterStatus::StopIteration;
  }

  default:
    ABSL_UNREACHABLE();
  }
}

DecoderPtr SmtpFilter::createDecoder() { return std::make_unique<DecoderImpl>(); }

bool SmtpFilter::onDownstreamSSLRequest() {
  ENVOY_CONN_LOG(trace, "smtp_proxy: onDownstreamSSLRequest()", read_callbacks_->connection());

  Buffer::OwnedImpl buf("220 envoy ready for tls\r\n");
  size_t len = buf.length();
  // Add callback to be notified when the reply message has been
  // transmitted.
  // TODO(jsbucy): does this need to handle bytes < len i.e. didn't
  // send the whole response in one shot?
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
