#pragma once

#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/filters/network/postgres_proxy/postgres_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

/**
 * All Postgres proxy stats. @see stats_macros.h
 */
#define ALL_POSTGRES_PROXY_STATS(COUNTER)                                                          \
  COUNTER(backend_msgs)                                                                            \
  COUNTER(errors)                                                                                  \
  COUNTER(errors_error)                                                                            \
  COUNTER(errors_fatal)                                                                            \
  COUNTER(errors_panic)                                                                            \
  COUNTER(errors_unknown)                                                                          \
  COUNTER(frontend_msgs)                                                                           \
  COUNTER(sessions)                                                                                \
  COUNTER(sessions_encrypted)                                                                      \
  COUNTER(sessions_unencrypted)                                                                    \
  COUNTER(statements)                                                                              \
  COUNTER(statements_insert)                                                                       \
  COUNTER(statements_delete)                                                                       \
  COUNTER(statements_update)                                                                       \
  COUNTER(statements_select)                                                                       \
  COUNTER(statements_other)                                                                        \
  COUNTER(transactions)                                                                            \
  COUNTER(transactions_commit)                                                                     \
  COUNTER(transactions_rollback)                                                                   \
  COUNTER(unknown)                                                                                 \
  COUNTER(notices)                                                                                 \
  COUNTER(notices_warning)                                                                         \
  COUNTER(notices_notice)                                                                          \
  COUNTER(notices_debug)                                                                           \
  COUNTER(notices_info)                                                                            \
  COUNTER(notices_log)                                                                             \
  COUNTER(notices_unknown)

/**
 * Struct definition for all Postgres proxy stats. @see stats_macros.h
 */
struct PostgresProxyStats {
  ALL_POSTGRES_PROXY_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the Postgres proxy filter.
 */
class PostgresFilterConfig {
public:
  PostgresFilterConfig(const std::string& stat_prefix, Stats::Scope& scope);

  const std::string stat_prefix_;
  Stats::Scope& scope_;
  PostgresProxyStats stats_;

private:
  PostgresProxyStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return PostgresProxyStats{ALL_POSTGRES_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
};

using PostgresFilterConfigSharedPtr = std::shared_ptr<PostgresFilterConfig>;

class PostgresFilter : public Network::Filter,
                       DecoderCallbacks,
                       Logger::Loggable<Logger::Id::filter> {
public:
  PostgresFilter(PostgresFilterConfigSharedPtr config);
  ~PostgresFilter() override = default;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

  // PostgresProxy::DecoderCallback
  void incBackend() override;
  void incFrontend() override;
  void incSessionsEncrypted() override;
  void incSessionsUnencrypted() override;
  void incStatement(StatementType) override;
  void incTransactions() override;
  void incTransactionsCommit() override;
  void incTransactionsRollback() override;
  void incUnknown() override;
  void incNotice(NoticeType) override;
  void incError(ErrorType) override;

  void doDecode(Buffer::Instance& data, bool);
  DecoderPtr createDecoder(DecoderCallbacks* callbacks);
  void setDecoder(std::unique_ptr<Decoder> decoder) { decoder_ = std::move(decoder); }
  Decoder* getDecoder() const { return decoder_.get(); }

  uint32_t getFrontendBufLength() const { return frontend_buffer_.length(); }
  uint32_t getBackendBufLength() const { return backend_buffer_.length(); }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  PostgresFilterConfigSharedPtr config_;
  Buffer::OwnedImpl frontend_buffer_;
  Buffer::OwnedImpl backend_buffer_;
  std::unique_ptr<Decoder> decoder_;
};

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
