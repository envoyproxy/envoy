#include "contrib/smtp_proxy/filters/network/source/smtp_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

SmtpFilterConfig::SmtpFilterConfig(const SmtpFilterConfigOptions& config_options,
                                           Stats::Scope& scope)
    : enable_sql_parsing_(config_options.enable_sql_parsing_),
      terminate_ssl_(config_options.terminate_ssl_), upstream_ssl_(config_options.upstream_ssl_),
      scope_{scope}, stats_{generateStats(config_options.stats_prefix_, scope)} {}

SmtpFilter::SmtpFilter(SmtpFilterConfigSharedPtr config) : config_{config} {
  if (!decoder_) {
    decoder_ = createDecoder(this);
  }
}

// Network::ReadFilter
Network::FilterStatus SmtpFilter::onData(Buffer::Instance& data, bool) {
  ENVOY_CONN_LOG(trace, "smtp_proxy: got {} bytes", read_callbacks_->connection(),
                 data.length());

  // Frontend Buffer
  frontend_buffer_.add(data);
  Network::FilterStatus result = doDecode(frontend_buffer_, true);
  if (result == Network::FilterStatus::StopIteration) {
    ASSERT(frontend_buffer_.length() == 0);
    data.drain(data.length());
  }
  return result;
}

Network::FilterStatus SmtpFilter::onNewConnection() { return Network::FilterStatus::Continue; }

void SmtpFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

// Network::WriteFilter
Network::FilterStatus SmtpFilter::onWrite(Buffer::Instance& data, bool) {

  // Backend Buffer
  backend_buffer_.add(data);
  Network::FilterStatus result = doDecode(backend_buffer_, false);
  if (result == Network::FilterStatus::StopIteration) {
    ASSERT(backend_buffer_.length() == 0);
    data.drain(data.length());
  }
  return result;
}

DecoderPtr SmtpFilter::createDecoder(DecoderCallbacks* callbacks) {
  return std::make_unique<DecoderImpl>(callbacks);
}

void SmtpFilter::incMessagesBackend() {
  config_->stats_.messages_.inc();
  config_->stats_.messages_backend_.inc();
}

void SmtpFilter::incMessagesFrontend() {
  config_->stats_.messages_.inc();
  config_->stats_.messages_frontend_.inc();
}

void SmtpFilter::incMessagesUnknown() {
  config_->stats_.messages_.inc();
  config_->stats_.messages_unknown_.inc();
}

void SmtpFilter::incSessionsEncrypted() {
  config_->stats_.sessions_.inc();
  config_->stats_.sessions_encrypted_.inc();
}

void SmtpFilter::incSessionsUnencrypted() {
  config_->stats_.sessions_.inc();
  config_->stats_.sessions_unencrypted_.inc();
}

void SmtpFilter::incTransactions() {
  if (!decoder_->getSession().inTransaction()) {
    config_->stats_.transactions_.inc();
  }
}

void SmtpFilter::incTransactionsCommit() {
  if (!decoder_->getSession().inTransaction()) {
    config_->stats_.transactions_commit_.inc();
  }
}

void SmtpFilter::incTransactionsRollback() {
  if (decoder_->getSession().inTransaction()) {
    config_->stats_.transactions_rollback_.inc();
  }
}

void SmtpFilter::incNotices(NoticeType type) {
  config_->stats_.notices_.inc();
  switch (type) {
  case DecoderCallbacks::NoticeType::Warning:
    config_->stats_.notices_warning_.inc();
    break;
  case DecoderCallbacks::NoticeType::Notice:
    config_->stats_.notices_notice_.inc();
    break;
  case DecoderCallbacks::NoticeType::Debug:
    config_->stats_.notices_debug_.inc();
    break;
  case DecoderCallbacks::NoticeType::Info:
    config_->stats_.notices_info_.inc();
    break;
  case DecoderCallbacks::NoticeType::Log:
    config_->stats_.notices_log_.inc();
    break;
  case DecoderCallbacks::NoticeType::Unknown:
    config_->stats_.notices_unknown_.inc();
    break;
  }
}

void SmtpFilter::incErrors(ErrorType type) {
  config_->stats_.errors_.inc();
  switch (type) {
  case DecoderCallbacks::ErrorType::Error:
    config_->stats_.errors_error_.inc();
    break;
  case DecoderCallbacks::ErrorType::Fatal:
    config_->stats_.errors_fatal_.inc();
    break;
  case DecoderCallbacks::ErrorType::Panic:
    config_->stats_.errors_panic_.inc();
    break;
  case DecoderCallbacks::ErrorType::Unknown:
    config_->stats_.errors_unknown_.inc();
    break;
  }
}

void SmtpFilter::incStatements(StatementType type) {
  config_->stats_.statements_.inc();

  switch (type) {
  case DecoderCallbacks::StatementType::Insert:
    config_->stats_.statements_insert_.inc();
    break;
  case DecoderCallbacks::StatementType::Delete:
    config_->stats_.statements_delete_.inc();
    break;
  case DecoderCallbacks::StatementType::Select:
    config_->stats_.statements_select_.inc();
    break;
  case DecoderCallbacks::StatementType::Update:
    config_->stats_.statements_update_.inc();
    break;
  case DecoderCallbacks::StatementType::Other:
    config_->stats_.statements_other_.inc();
    break;
  case DecoderCallbacks::StatementType::Noop:
    break;
  }
}

void SmtpFilter::processQuery(const std::string& sql) {
  if (config_->enable_sql_parsing_) {
    ProtobufWkt::Struct metadata;

    auto result = Common::SQLUtils::SQLUtils::setMetadata(sql, decoder_->getAttributes(), metadata);

    if (!result) {
      config_->stats_.statements_parse_error_.inc();
      ENVOY_CONN_LOG(trace, "smtp_proxy: cannot parse SQL: {}", read_callbacks_->connection(),
                     sql.c_str());
      return;
    }

    config_->stats_.statements_parsed_.inc();
    ENVOY_CONN_LOG(trace, "smtp_proxy: query processed {}", read_callbacks_->connection(),
                   sql.c_str());

    // Set dynamic metadata
    read_callbacks_->connection().streamInfo().setDynamicMetadata(
        NetworkFilterNames::get().SmtpProxy, metadata);
  }
}

bool SmtpFilter::onSSLRequest() {
  if (!config_->terminate_ssl_) {
    // Signal to the decoder to continue.
    return true;
  }
  // Send single bytes 'S' to indicate switch to TLS.
  // Refer to official documentation for protocol details:
  // https://www.smtpql.org/docs/current/protocol-flow.html
  Buffer::OwnedImpl buf;
  buf.add("S");
  // Add callback to be notified when the reply message has been
  // transmitted.
  read_callbacks_->connection().addBytesSentCallback([=](uint64_t bytes) -> bool {
    // Wait until 'S' has been transmitted.
    if (bytes >= 1) {
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
        // Switch to TLS has been completed.
        // Signal to the decoder to stop processing the current message (SSLRequest).
        // Because Envoy terminates SSL, the message was consumed and should not be
        // passed to other filters in the chain.
        return false;
      }
    }
    return true;
  });
  read_callbacks_->connection().write(buf, false);

  return false;
}

bool SmtpFilter::shouldEncryptUpstream() const {
  return (config_->upstream_ssl_ ==
          envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::REQUIRE);
}

void SmtpFilter::sendUpstream(Buffer::Instance& data) {
  read_callbacks_->injectReadDataToFilterChain(data, false);
}

void SmtpFilter::encryptUpstream(bool upstream_agreed, Buffer::Instance& data) {
  RELEASE_ASSERT(
      config_->upstream_ssl_ !=
          envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::DISABLE,
      "encryptUpstream should not be called when upstream SSL is disabled.");
  if (!upstream_agreed) {
    ENVOY_CONN_LOG(info,
                   "smtp_proxy: upstream server rejected request to establish SSL connection. "
                   "Terminating.",
                   read_callbacks_->connection());
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);

    config_->stats_.sessions_upstream_ssl_failed_.inc();
  } else {
    // Try to switch upstream connection to use a secure channel.
    if (read_callbacks_->startUpstreamSecureTransport()) {
      config_->stats_.sessions_upstream_ssl_success_.inc();
      read_callbacks_->injectReadDataToFilterChain(data, false);
      ENVOY_CONN_LOG(trace, "smtp_proxy: upstream SSL enabled.", read_callbacks_->connection());
    } else {
      ENVOY_CONN_LOG(info,
                     "smtp_proxy: cannot enable upstream secure transport. Check "
                     "configuration. Terminating.",
                     read_callbacks_->connection());
      read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
      config_->stats_.sessions_upstream_ssl_failed_.inc();
    }
  }
}

Network::FilterStatus SmtpFilter::doDecode(Buffer::Instance& data, bool frontend) {
  // Keep processing data until buffer is empty or decoder says
  // that it cannot process data in the buffer.
  while (0 < data.length()) {
    switch (decoder_->onData(data, frontend)) {
    case Decoder::Result::NeedMoreData:
      return Network::FilterStatus::Continue;
    case Decoder::Result::ReadyForNext:
      continue;
    case Decoder::Result::Stopped:
      return Network::FilterStatus::StopIteration;
    }
  }
  return Network::FilterStatus::Continue;
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
