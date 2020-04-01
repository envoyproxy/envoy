#include "extensions/filters/network/postgresql_proxy/postgresql_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "extensions/filters/network/postgresql_proxy/postgresql_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

PostgreSQLFilterConfig::PostgreSQLFilterConfig(const std::string& stat_prefix, Stats::Scope& scope)
    : stat_prefix_{stat_prefix}, scope_{scope}, stats_{generateStats(stat_prefix, scope)} {}

PostgreSQLFilter::PostgreSQLFilter(PostgreSQLFilterConfigSharedPtr config) : config_{config} {
  if (!decoder_) {
    decoder_ = createDecoder(this);
  }
}

// Network::ReadFilter
Network::FilterStatus PostgreSQLFilter::onData(Buffer::Instance& data, bool) {
  ENVOY_CONN_LOG(trace, "echo: got {} bytes", read_callbacks_->connection(), data.length());

  // Frontend Buffer
  frontend_buffer_.add(data);
  doDecode(frontend_buffer_, true);

  return Network::FilterStatus::Continue;
}

Network::FilterStatus PostgreSQLFilter::onNewConnection() {
  return Network::FilterStatus::Continue;
}

void PostgreSQLFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

// Network::WriteFilter
Network::FilterStatus PostgreSQLFilter::onWrite(Buffer::Instance& data, bool) {

  // Backend Buffer
  backend_buffer_.add(data);
  doDecode(backend_buffer_, false);

  return Network::FilterStatus::Continue;
}

DecoderPtr PostgreSQLFilter::createDecoder(DecoderCallbacks* callbacks) {
  return std::make_unique<DecoderImpl>(callbacks);
}

void PostgreSQLFilter::incBackend() { config_->stats_.backend_msgs_.inc(); }

void PostgreSQLFilter::incFrontend() { config_->stats_.frontend_msgs_.inc(); }

void PostgreSQLFilter::incSessionsEncrypted() {
  config_->stats_.sessions_.inc();
  config_->stats_.sessions_encrypted_.inc();
}

void PostgreSQLFilter::incSessionsUnencrypted() {
  config_->stats_.sessions_.inc();
  config_->stats_.sessions_unencrypted_.inc();
}

void PostgreSQLFilter::incTransactions() {
  if (!decoder_->getSession().inTransaction()) {
    config_->stats_.transactions_.inc();
  }
}

void PostgreSQLFilter::incTransactionsCommit() {
  if (!decoder_->getSession().inTransaction()) {
    config_->stats_.transactions_commit_.inc();
  }
}

void PostgreSQLFilter::incTransactionsRollback() {
  if (!decoder_->getSession().inTransaction()) {
    config_->stats_.transactions_rollback_.inc();
  }
}

void PostgreSQLFilter::incNotice(NoticeType type) {
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
    config_->stats_.notices_debug_.inc();
    break;
  case DecoderCallbacks::NoticeType::Log:
    config_->stats_.notices_log_.inc();
    break;
  case DecoderCallbacks::NoticeType::Unknown:
    config_->stats_.notices_unknown_.inc();
    break;
  }
}

void PostgreSQLFilter::incError(ErrorType type) {
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

void PostgreSQLFilter::incStatement(StatementType type) {
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

void PostgreSQLFilter::incUnknown() { config_->stats_.unknown_.inc(); }

void PostgreSQLFilter::doDecode(Buffer::Instance& data, bool frontend) {
  // Keep processing data until buffer is empty or decoder says
  // that it cannot process data in the buffer.
  while ((0 < data.length()) && (decoder_->onData(data, frontend))) {
    ;
  }
}

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
