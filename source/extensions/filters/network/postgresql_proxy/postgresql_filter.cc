#include "extensions/filters/network/postgresql_proxy/postgresql_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "extensions/filters/network/postgresql_proxy/postgresql_decoder.h"
#include "extensions/filters/network/postgresql_proxy/postgresql_utils.h"

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
  // decoder_->getSession().setProtocolDirection(PostgreSQLSession::ProtocolDirection::Frontend);
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
  //  decoder_->getSession().setProtocolDirection(PostgreSQLSession::ProtocolDirection::Backend);
  doDecode(backend_buffer_, false);

  return Network::FilterStatus::Continue;
}

DecoderPtr PostgreSQLFilter::createDecoder(DecoderCallbacks* callbacks) {
  return std::make_unique<DecoderImpl>(callbacks);
}

void PostgreSQLFilter::incErrors() { config_->stats_.errors_.inc(); }

void PostgreSQLFilter::incSessions() { config_->stats_.sessions_.inc(); }

void PostgreSQLFilter::incStatements() { config_->stats_.statements_.inc(); }

void PostgreSQLFilter::incFrontend() { config_->stats_.frontend_commands_.inc(); }

void PostgreSQLFilter::incUnrecognized() { config_->stats_.unrecognized_.inc(); }

void PostgreSQLFilter::incStatementsDelete() {
  config_->stats_.statements_delete_.inc();
  incTransactions();
}

void PostgreSQLFilter::incStatementsInsert() {
  config_->stats_.statements_insert_.inc();
  incTransactions();
}

void PostgreSQLFilter::incStatementsOther() {
  config_->stats_.statements_other_.inc();
  incTransactions();
}

void PostgreSQLFilter::incStatementsSelect() {
  config_->stats_.statements_select_.inc();
  incTransactions();
}

void PostgreSQLFilter::incStatementsUpdate() {
  config_->stats_.statements_update_.inc();
  incTransactions();
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

void PostgreSQLFilter::incWarnings() { config_->stats_.warnings_.inc(); }

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
