#include "contrib/smtp_proxy/filters/network/source/smtp_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"
#include "source/extensions/filters/network/well_known_names.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"

#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"

using Envoy::Extensions::Common::ProxyProtocol::generateProxyProtoHeader;


namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

SmtpFilterConfig::SmtpFilterConfig(const SmtpFilterConfigOptions& config_options,
                                           Stats::Scope& scope)
    : proxy_protocol_config_(config_options.proxy_protocol_config_),
      terminate_ssl_(config_options.terminate_ssl_), upstream_ssl_(config_options.upstream_ssl_),
      scope_{scope}, stats_{generateStats(config_options.stats_prefix_, scope)} {}

SmtpFilter::SmtpFilter(SmtpFilterConfigSharedPtr config) : config_{config} {
  if (!decoder_) {
    decoder_ = createDecoder();
  }
}

Network::FilterStatus SmtpFilter::onNewConnection() {
  Buffer::OwnedImpl banner("220 envoy ESMTP\r\n");
  write_callbacks_->injectWriteDataToFilterChain(banner, /*end_stream=*/false);
  state_ = EHLO;
  return Network::FilterStatus::Continue;
}

Network::FilterStatus SmtpFilter::readUpstream(State new_state) {
  state_ = new_state;
  // may need to wake up writer
  if (backend_buffer_.length() > 0) {
    Buffer::OwnedImpl empty;
    onWrite(empty, false);
  }
  return Network::FilterStatus::StopIteration; 
}

Network::FilterStatus SmtpFilter::readDownstream(State new_state) {
  state_ = new_state;
  // may need to wake up writer
  if (frontend_buffer_.length() > 0) {
    Buffer::OwnedImpl empty;
    onData(empty, false);
  }
  return Network::FilterStatus::StopIteration; 
}

Network::FilterStatus SmtpFilter::maybeSendProxyHeader(State next) {
  if (config_->proxy_protocol_config_) {
    Buffer::OwnedImpl proxy_header;
    // Note: this doesn't seem to populate tls info
    generateProxyProtoHeader(*config_->proxy_protocol_config_,
			     read_callbacks_->connection(),
			     proxy_header);
    read_callbacks_->injectReadDataToFilterChain(proxy_header, false);
  }

  return readUpstream(next);
}

// Network::ReadFilter
Network::FilterStatus SmtpFilter::onData(Buffer::Instance& data, bool) {
  ENVOY_CONN_LOG(trace, "smtp_proxy: onData {} bytes, state {}", read_callbacks_->connection(),
                 data.length(), state_);

  if (state_ == PASSTHROUGH) {
    return Network::FilterStatus::Continue;
  }

  frontend_buffer_.add(data);
  data.drain(data.length());

  Decoder::Command command;
  if (state_ == EHLO ||
      state_ == STARTTLS ||
      state_ == EHLO2) {
    Decoder::Result res = decoder_->DecodeCommand(frontend_buffer_, command);

    if (res == Decoder::Result::Bad) {
      Buffer::OwnedImpl err("500 bad\r\n");
      write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
      // TODO may not always want to hang up here
      read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      return Network::FilterStatus::StopIteration; 
    }
    if (res != Decoder::Result::ReadyForNext) {
      return Network::FilterStatus::Continue;
    }

    if (command.wire_len != frontend_buffer_.length()) {
      ENVOY_CONN_LOG(trace, "smtp_proxy: bad pipeline {} {}", read_callbacks_->connection(),
		     command.wire_len, frontend_buffer_.length());

      Buffer::OwnedImpl err("503 bad pipeline\r\n");
      write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
      read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      return Network::FilterStatus::StopIteration; 
    }

    ENVOY_CONN_LOG(trace, "smtp_proxy: state {} command {}", read_callbacks_->connection(),
                   state_, command.verb);
  }

  switch (state_) {
    case EHLO: {
      if (command.verb != "helo" && command.verb != "ehlo") {
	Buffer::OwnedImpl err("503 bad sequence of commands\r\n");
	write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
	read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
	return Network::FilterStatus::StopIteration; 
      }
      if (command.verb == "helo") {
	return maybeSendProxyHeader(UPSTREAM_RESPONSE);
      }
      Buffer::OwnedImpl esmtp;
      // XXX per config
      esmtp.add("250-envoy smtp\r\n"
		"250 STARTTLS\r\n");
      frontend_buffer_.drain(command.wire_len);

      write_callbacks_->injectWriteDataToFilterChain(esmtp, /*end_stream=*/false);
      state_ = STARTTLS;
      return Network::FilterStatus::StopIteration; 
    }
    case STARTTLS: {
      if (command.verb != "starttls") {
	return maybeSendProxyHeader(UPSTREAM_BANNER);
      }
      frontend_buffer_.drain(command.wire_len);
      onSSLRequest();
      return Network::FilterStatus::StopIteration; 
    }
    case EHLO2: {
      // If the next command from the client after TLS isn't ehlo:
      // leave the command in frontend_buffer_
      // emit an ehlo to the upstream
      // consume the ehlo response
      // inject the buffered command from frontend_buffer_ to the upstream
      if (command.verb != "ehlo") {
	Buffer::OwnedImpl ehlo2("ehlo envoy\r\n");
	read_callbacks_->injectReadDataToFilterChain(ehlo2, false);
	state_ = UPSTREAM_RESPONSE;
	return Network::FilterStatus::StopIteration; 
      }
      
      read_callbacks_->injectReadDataToFilterChain(frontend_buffer_, /*end_stream=*/false);
      //XXX ASSERT(frontend_buffer_.length() == command.wire_len);  // bad pipeline above
      frontend_buffer_.drain(frontend_buffer_.length());

      state_ = PASSTHROUGH;
      return Network::FilterStatus::StopIteration; 
    }
  default:
    // TODO this probably means that the client spoke at the wrong time
    break;
  }

  return Network::FilterStatus::StopIteration;
}

void SmtpFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

void SmtpFilter::initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) {
  write_callbacks_ = &callbacks;
}

// Network::WriteFilter
Network::FilterStatus SmtpFilter::onWrite(Buffer::Instance& data, bool) {
  ENVOY_CONN_LOG(trace, "smtp_proxy: onWrite {} bytes, state {}", read_callbacks_->connection(),
                 data.length(), state_);

  if (state_ == PASSTHROUGH) {
    return Network::FilterStatus::Continue;
  }
  backend_buffer_.add(data);
  data.drain(data.length());

  // upstream will generally send its greeting immediately upon
  // connect so buffer it until we're ready for it.
  // TODO but some states are invalid for the server to send e.g. while we're waiting for the client
  if (state_ != UPSTREAM_BANNER &&
      state_ != UPSTREAM_RESPONSE) {
    return Network::FilterStatus::StopIteration;
  }

  // We need to decode the response to know that we consumed it all
  Decoder::Response response;
  Decoder::Result res = decoder_->DecodeResponse(backend_buffer_, response);
  if (res == Decoder::Result::Bad) {
    Buffer::OwnedImpl err("451 internal error\r\n");
    write_callbacks_->injectWriteDataToFilterChain(err, /*end_stream=*/true);
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return Network::FilterStatus::StopIteration; 
  }
  if (res != Decoder::Result::ReadyForNext) {
    return Network::FilterStatus::Continue;
  }
  backend_buffer_.drain(response.wire_len);

  if (state_ == UPSTREAM_BANNER) {
    // we may have buffered the next command while we were waiting for this, resume processing that
    return readDownstream(EHLO2);
  } else {  // UPSTREAM_RESPONSE
    // after filter-emitted 2nd ehlo: consumed ehlo response, send buffered command (mail, etc)
    // after client helo: consumed banner, send buffered helo
    read_callbacks_->injectReadDataToFilterChain(frontend_buffer_, false);
    frontend_buffer_.drain(frontend_buffer_.length());
    state_ = PASSTHROUGH;
  }

  return Network::FilterStatus::StopIteration;
}

DecoderPtr SmtpFilter::createDecoder() {
  return std::make_unique<DecoderImpl>();
}

bool SmtpFilter::onSSLRequest() {
  ENVOY_CONN_LOG(trace, "smtp_proxy: onSSLRequest()",
		 read_callbacks_->connection());

  Buffer::OwnedImpl buf("220 envoy ready for tls\r\n");
  size_t len = buf.length();
  // Add callback to be notified when the reply message has been
  // transmitted.
  read_callbacks_->connection().addBytesSentCallback([=](uint64_t bytes) -> bool {
    // Wait until response has been transmitted.
    if (bytes >= len) {
      if (!read_callbacks_->connection().startSecureTransport()) {
        ENVOY_CONN_LOG(
            info, "smtp_proxy: cannot enable downstream secure transport. Check configuration.",
            read_callbacks_->connection());
        read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
      } else {
        // Unsubscribe the callback.
        config_->stats_.sessions_terminated_ssl_.inc();
        ENVOY_CONN_LOG(trace, "smtp_proxy: enabled SSL termination.",
                       read_callbacks_->connection());

	// if proxy header enabled, injectReadDataToFilterChain()
	maybeSendProxyHeader(UPSTREAM_BANNER);

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
