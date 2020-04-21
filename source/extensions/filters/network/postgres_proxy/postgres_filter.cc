#include "extensions/filters/network/postgres_proxy/postgres_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "extensions/filters/network/postgres_proxy/postgres_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

PostgresFilterConfig::PostgresFilterConfig(const std::string& stat_prefix, Stats::Scope& scope)
    : stat_prefix_{stat_prefix}, scope_{scope}, stats_{generateStats(stat_prefix, scope)} {}

PostgresFilter::PostgresFilter(PostgresFilterConfigSharedPtr config) : config_{config} {
  if (!decoder_) {
    decoder_ = createDecoder(this);
  }
}

// Network::ReadFilter
Network::FilterStatus PostgresFilter::onData(Buffer::Instance& data, bool) {
  ENVOY_CONN_LOG(trace, "echo: got {} bytes", read_callbacks_->connection(), data.length());

  // Frontend Buffer
  frontend_buffer_.add(data);
  doDecode(frontend_buffer_, true);

  return Network::FilterStatus::Continue;
}

Network::FilterStatus PostgresFilter::onNewConnection() { return Network::FilterStatus::Continue; }

void PostgresFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

// Network::WriteFilter
Network::FilterStatus PostgresFilter::onWrite(Buffer::Instance& data, bool) {

  // Backend Buffer
  backend_buffer_.add(data);
  doDecode(backend_buffer_, false);

  return Network::FilterStatus::Continue;
}

DecoderPtr PostgresFilter::createDecoder(DecoderCallbacks* callbacks) {
  return std::make_unique<DecoderImpl>(callbacks);
}

void PostgresFilter::incMessagesBackend() {
  config_->stats_.messages_.inc();
  config_->stats_.messages_backend_.inc();
}

void PostgresFilter::incMessagesFrontend() {
  config_->stats_.messages_.inc();
  config_->stats_.messages_frontend_.inc();
}

void PostgresFilter::incMessagesUnknown() {
  config_->stats_.messages_.inc();
  config_->stats_.messages_unknown_.inc();
}

void PostgresFilter::incSessionsEncrypted() {
  config_->stats_.sessions_.inc();
  config_->stats_.sessions_encrypted_.inc();
}

void PostgresFilter::incSessionsUnencrypted() {
  config_->stats_.sessions_.inc();
  config_->stats_.sessions_unencrypted_.inc();
}

void PostgresFilter::incTransactions() {
  if (!decoder_->getSession().inTransaction()) {
    config_->stats_.transactions_.inc();
  }
}

void PostgresFilter::incTransactionsCommit() {
  if (!decoder_->getSession().inTransaction()) {
    config_->stats_.transactions_commit_.inc();
  }
}

void PostgresFilter::incTransactionsRollback() {
  if (!decoder_->getSession().inTransaction()) {
    config_->stats_.transactions_rollback_.inc();
  }
}

void PostgresFilter::incNotices(NoticeType type) {
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

void PostgresFilter::incErrors(ErrorType type) {
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

void PostgresFilter::incStatements(StatementType type) {
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

void PostgresFilter::doDecode(Buffer::Instance& data, bool frontend) {
  // Keep processing data until buffer is empty or decoder says
  // that it cannot process data in the buffer.
  while ((0 < data.length()) && (decoder_->onData(data, frontend))) {
    ;
  }
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
