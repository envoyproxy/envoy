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

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/network/drain_close_util.h"
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
  ProxyFilter(Runtime::Loader& runtime, AccessLogSharedPtr access_log,
              const Filters::Common::Fault::FaultDelayConfigSharedPtr& fault_config,
              const Network::DrainDecision& drain_decision,
              Server::Configuration::ServerFactoryContext& server_context, TimeSource& time_system,
              bool emit_dynamic_metadata, const MongoStatsSharedPtr& stats,
              uint32_t max_bson_depth);
  ~ProxyFilter() override;

  virtual DecoderPtr createDecoder(DecoderCallbacks& callbacks) PURE;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    // Captured once here rather than plumbed through the filter factory: the drain type belongs to
    // the listener that accepted this connection, and is reachable from the connection itself.
    drain_type_ = Network::listenerDrainType(read_callbacks_->connection());
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
  // Only records the event; the drain-close decision is made per reply in shouldDrainClose().
  void onDrain(Network::ConnectionDrainEvent drain_event) override {
    if (!connection_drain_event_.has_value()) {
      connection_drain_event_ = drain_event;
    }
  }

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

  // Increment counters related to queries. 'base_names' and 'names' are passed by non-const
  // reference so the implementation can mutate them without copying, though it
  // always restores them to their prior state prior to return.
  void chargeQueryStats(Stats::StatNameVec& base_names, Stats::StatNameTagSpan tags,
                        Stats::StatNameVec& names, QueryMessageInfo::QueryType query_type);

  // Add samples to histograms related to replies. 'base_names' and 'names' are passed by non-const
  // reference so the implementation can mutate them without copying, though it
  // always restores them to their prior state prior to return.
  void chargeReplyStats(ActiveQuery& active_query, Stats::StatNameVec& base_names,
                        Stats::StatNameTagSpan tags, Stats::StatNameVec& names,
                        const ReplyMessage& message);

  void doDecode(Buffer::Instance& buffer);
  void logMessage(Message& message, bool full);
  // Returns true if the connection should be drain-closed.
  bool shouldDrainClose();
  void onDrainClose();
  std::optional<std::chrono::milliseconds> delayDuration();
  void delayInjectionTimerCallback();
  void tryInjectDelay();

  // Declared first so it is destroyed last: stats_ references the MongoProxyStats it owns, and
  // other members (e.g. active_query_list_) touch stats_ from their destructors.
  MongoStatsSharedPtr mongo_stats_;
  std::unique_ptr<Decoder> decoder_;
  // References the per-configuration stats owned by the shared MongoStats, which is kept alive
  // by mongo_stats_.
  MongoProxyStats& stats_;
  Runtime::Loader& runtime_;
  const Network::DrainDecision& drain_decision_;
  Server::Configuration::ServerFactoryContext& server_context_;
  // The drain type of the listener owning this connection, used to decide whether
  // /healthcheck/fail should drain-close it.
  envoy::config::listener::v3::Listener::DrainType drain_type_{
      envoy::config::listener::v3::Listener::DEFAULT};
  // Set when the connection is notified of a drain sequence via onDrain().
  std::optional<Network::ConnectionDrainEvent> connection_drain_event_;
  // Latched when the filter is created so it is not re-read on every reply. See shouldDrainClose().
  const bool use_connection_event_drain_ = false;
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

protected:
  const uint32_t max_bson_depth_;
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
