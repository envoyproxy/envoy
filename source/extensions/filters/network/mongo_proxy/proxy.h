#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/common/time.h"
#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/network/filter_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/filters/common/fault/fault_config.h"
#include "source/extensions/filters/network/mongo_proxy/codec.h"
#include "source/extensions/filters/network/mongo_proxy/mongo_stats.h"
#include "source/extensions/filters/network/mongo_proxy/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

class MongoRuntimeConfigKeys {
public:
  const std::string FixedDelayPercent{"mongo.fault.fixed_delay.percent"};
  const std::string FixedDelayDurationMs{"mongo.fault.fixed_delay.duration_ms"};
  const std::string LoggingEnabled{"mongo.logging_enabled"};
  const std::string ProxyEnabled{"mongo.proxy_enabled"};
  const std::string ConnectionLoggingEnabled{"mongo.connection_logging_enabled"};
  const std::string DrainCloseEnabled{"mongo.drain_close_enabled"};
};

using MongoRuntimeConfig = ConstSingleton<MongoRuntimeConfigKeys>;

/**
 * All mongo proxy stats. @see stats_macros.h
 */
#define ALL_MONGO_PROXY_STATS(COUNTER, GAUGE, HISTOGRAM)                                           \
  COUNTER(cx_destroy_local_with_active_rq)                                                         \
  COUNTER(cx_destroy_remote_with_active_rq)                                                        \
  COUNTER(cx_drain_close)                                                                          \
  COUNTER(decoding_error)                                                                          \
  COUNTER(delays_injected)                                                                         \
  COUNTER(op_command)                                                                              \
  COUNTER(op_command_reply)                                                                        \
  COUNTER(op_get_more)                                                                             \
  COUNTER(op_insert)                                                                               \
  COUNTER(op_kill_cursors)                                                                         \
  COUNTER(op_query)                                                                                \
  COUNTER(op_query_await_data)                                                                     \
  COUNTER(op_query_exhaust)                                                                        \
  COUNTER(op_query_multi_get)                                                                      \
  COUNTER(op_query_no_cursor_timeout)                                                              \
  COUNTER(op_query_no_max_time)                                                                    \
  COUNTER(op_query_scatter_get)                                                                    \
  COUNTER(op_query_tailable_cursor)                                                                \
  COUNTER(op_reply)                                                                                \
  COUNTER(op_reply_cursor_not_found)                                                               \
  COUNTER(op_reply_query_failure)                                                                  \
  COUNTER(op_reply_valid_cursor)                                                                   \
  GAUGE(op_query_active, Accumulate)

/**
 * Struct definition for all mongo proxy stats. @see stats_macros.h
 */
struct MongoProxyStats {
  ALL_MONGO_PROXY_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

/**
 * Access logger for mongo messages.
 */
class AccessLog {
public:
  AccessLog(const std::string& file_name, Envoy::AccessLog::AccessLogManager& log_manager,
            TimeSource& time_source);

  void logMessage(const Message& message, bool full,
                  const Upstream::HostDescription* upstream_host);

private:
  TimeSource& time_source_;
  Envoy::AccessLog::AccessLogFileSharedPtr file_;
};

using AccessLogSharedPtr = std::shared_ptr<AccessLog>;

/**
 * A sniffing filter for mongo traffic. The current implementation makes a copy of read/written
 * data, decodes it, and generates stats.
 */
class ProxyFilter : public Network::Filter,
                    public DecoderCallbacks,
                    public Network::ConnectionCallbacks,
                    Logger::Loggable<Logger::Id::mongo> {
public:
  ProxyFilter(const std::string& stat_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
              AccessLogSharedPtr access_log,
              const Filters::Common::Fault::FaultDelayConfigSharedPtr& fault_config,
              const Network::DrainDecision& drain_decision, TimeSource& time_system,
              bool emit_dynamic_metadata, const MongoStatsSharedPtr& stats);
  ~ProxyFilter() override;

  virtual DecoderPtr createDecoder(DecoderCallbacks& callbacks) PURE;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    read_callbacks_->connection().addConnectionCallbacks(*this);
  }

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

  // Mongo::DecoderCallback
  void decodeGetMore(GetMoreMessagePtr&& message) override;
  void decodeInsert(InsertMessagePtr&& message) override;
  void decodeKillCursors(KillCursorsMessagePtr&& message) override;
  void decodeQuery(QueryMessagePtr&& message) override;
  void decodeReply(ReplyMessagePtr&& message) override;
  void decodeCommand(CommandMessagePtr&& message) override;
  void decodeCommandReply(CommandReplyMessagePtr&& message) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  void setDynamicMetadata(std::string operation, std::string resource);

private:
  struct ActiveQuery {
    ActiveQuery(ProxyFilter& parent, const QueryMessage& query)
        : parent_(parent), query_info_(query), start_time_(parent_.time_source_.monotonicTime()) {
      parent_.stats_.op_query_active_.inc();
    }

    ~ActiveQuery() { parent_.stats_.op_query_active_.dec(); }

    ProxyFilter& parent_;
    QueryMessageInfo query_info_;
    MonotonicTime start_time_;
  };

  using ActiveQueryPtr = std::unique_ptr<ActiveQuery>;

  MongoProxyStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return MongoProxyStats{ALL_MONGO_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                                 POOL_GAUGE_PREFIX(scope, prefix),
                                                 POOL_HISTOGRAM_PREFIX(scope, prefix))};
  }

  // Increment counters related to queries. 'names' is passed by non-const
  // reference so the implementation can mutate it without copying, though it
  // always restores it to its prior state prior to return.
  void chargeQueryStats(Stats::ElementVec& names, QueryMessageInfo::QueryType query_type);

  // Add samples to histograms related to replies. 'names' is passed by
  // non-const reference so the implementation can mutate it without copying,
  // though it always restores it to its prior state prior to return.
  void chargeReplyStats(ActiveQuery& active_query, Stats::ElementVec& names,
                        const ReplyMessage& message);

  void doDecode(Buffer::Instance& buffer);
  void logMessage(Message& message, bool full);
  void onDrainClose();
  absl::optional<std::chrono::milliseconds> delayDuration();
  void delayInjectionTimerCallback();
  void tryInjectDelay();

  std::unique_ptr<Decoder> decoder_;
  MongoProxyStats stats_;
  Runtime::Loader& runtime_;
  const Network::DrainDecision& drain_decision_;
  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
  bool sniffing_{true};
  std::list<ActiveQueryPtr> active_query_list_;
  AccessLogSharedPtr access_log_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  const Filters::Common::Fault::FaultDelayConfigSharedPtr fault_config_;
  Event::TimerPtr delay_timer_;
  Event::TimerPtr drain_close_timer_;
  TimeSource& time_source_;
  const bool emit_dynamic_metadata_;
  MongoStatsSharedPtr mongo_stats_;
};

class ProdProxyFilter : public ProxyFilter {
public:
  using ProxyFilter::ProxyFilter;

  // ProxyFilter
  DecoderPtr createDecoder(DecoderCallbacks& callbacks) override;
};

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
