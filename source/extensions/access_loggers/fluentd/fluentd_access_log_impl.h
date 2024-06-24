#pragma once

#include <chrono>

#include "envoy/extensions/access_loggers/fluentd/v3/fluentd.pb.h"
#include "envoy/extensions/access_loggers/fluentd/v3/fluentd.pb.validate.h"

#include "source/common/formatter/substitution_formatter.h"
#include "source/extensions/access_loggers/common/access_log_base.h"
#include "source/extensions/access_loggers/fluentd/substitution_formatter.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Fluentd {

using FluentdAccessLogConfig =
    envoy::extensions::access_loggers::fluentd::v3::FluentdAccessLogConfig;
using FluentdAccessLogConfigSharedPtr = std::shared_ptr<FluentdAccessLogConfig>;

static constexpr uint64_t DefaultBaseBackoffIntervalMs = 500;
static constexpr uint64_t DefaultMaxBackoffIntervalFactor = 10;
static constexpr uint64_t DefaultBufferFlushIntervalMs = 1000;
static constexpr uint64_t DefaultMaxBufferSize = 16384;

// Entry represents a single Fluentd message, msgpack format based, as specified in:
// https://github.com/fluent/fluentd/wiki/Forward-Protocol-Specification-v1#entry
class Entry {
public:
  Entry(const Entry&) = delete;
  Entry& operator=(const Entry&) = delete;
  Entry(uint64_t time, std::vector<uint8_t>&& record) : time_(time), record_(record) {}

  const uint64_t time_;
  const std::vector<uint8_t> record_;
};

using EntryPtr = std::unique_ptr<Entry>;

class FluentdAccessLogger {
public:
  virtual ~FluentdAccessLogger() = default;

  /**
   * Send the Fluentd formatted message over the upstream TCP connection.
   */
  virtual void log(EntryPtr&& entry) PURE;
};

using FluentdAccessLoggerWeakPtr = std::weak_ptr<FluentdAccessLogger>;
using FluentdAccessLoggerSharedPtr = std::shared_ptr<FluentdAccessLogger>;

#define ACCESS_LOG_FLUENTD_STATS(COUNTER, GAUGE)                                                   \
  COUNTER(entries_lost)                                                                            \
  COUNTER(entries_buffered)                                                                        \
  COUNTER(events_sent)                                                                             \
  COUNTER(reconnect_attempts)                                                                      \
  COUNTER(connections_closed)

struct AccessLogFluentdStats {
  ACCESS_LOG_FLUENTD_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

class FluentdAccessLoggerImpl : public Tcp::AsyncTcpClientCallbacks,
                                public FluentdAccessLogger,
                                public Logger::Loggable<Logger::Id::client> {
public:
  FluentdAccessLoggerImpl(Upstream::ThreadLocalCluster& cluster, Tcp::AsyncTcpClientPtr client,
                          Event::Dispatcher& dispatcher, const FluentdAccessLogConfig& config,
                          BackOffStrategyPtr backoff_strategy, Stats::Scope& parent_scope);

  // Tcp::AsyncTcpClientCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}
  void onData(Buffer::Instance&, bool) override {}

  // FluentdAccessLogger
  void log(EntryPtr&& entry) override;

private:
  void flush();
  void connect();
  void maybeReconnect();
  void onBackoffCallback();
  void setDisconnected();
  void clearBuffer();

  bool disconnected_ = false;
  bool connecting_ = false;
  std::string tag_;
  std::string id_;
  uint32_t connect_attempts_{0};
  absl::optional<uint32_t> max_connect_attempts_{};
  const Stats::ScopeSharedPtr stats_scope_;
  AccessLogFluentdStats fluentd_stats_;
  std::vector<EntryPtr> entries_;
  uint64_t approximate_message_size_bytes_ = 0;
  Upstream::ThreadLocalCluster& cluster_;
  const BackOffStrategyPtr backoff_strategy_;
  const Tcp::AsyncTcpClientPtr client_;
  const std::chrono::milliseconds buffer_flush_interval_msec_;
  const uint64_t max_buffer_size_bytes_;
  const Event::TimerPtr retry_timer_;
  const Event::TimerPtr flush_timer_;
};

class FluentdAccessLoggerCache {
public:
  virtual ~FluentdAccessLoggerCache() = default;

  /**
   * Get existing logger or create a new one for the given configuration.
   * @return FluentdAccessLoggerSharedPtr ready for logging requests.
   */
  virtual FluentdAccessLoggerSharedPtr
  getOrCreateLogger(const FluentdAccessLogConfigSharedPtr config,
                    Random::RandomGenerator& random) PURE;
};

using FluentdAccessLoggerCacheSharedPtr = std::shared_ptr<FluentdAccessLoggerCache>;

class FluentdAccessLoggerCacheImpl : public Singleton::Instance, public FluentdAccessLoggerCache {
public:
  FluentdAccessLoggerCacheImpl(Upstream::ClusterManager& cluster_manager,
                               Stats::Scope& parent_scope, ThreadLocal::SlotAllocator& tls);

  FluentdAccessLoggerSharedPtr getOrCreateLogger(const FluentdAccessLogConfigSharedPtr config,
                                                 Random::RandomGenerator& random) override;

private:
  /**
   * Per-thread cache.
   */
  struct ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
    ThreadLocalCache(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

    Event::Dispatcher& dispatcher_;
    // Access loggers indexed by the hash of logger's configuration.
    absl::flat_hash_map<std::size_t, FluentdAccessLoggerWeakPtr> access_loggers_;
  };

  Upstream::ClusterManager& cluster_manager_;
  const Stats::ScopeSharedPtr stats_scope_;
  ThreadLocal::SlotPtr tls_slot_;
};

/**
 * Access log Instance that writes logs to a Fluentd.
 */
class FluentdAccessLog : public Common::ImplBase {
public:
  FluentdAccessLog(AccessLog::FilterPtr&& filter, FluentdFormatterPtr&& formatter,
                   const FluentdAccessLogConfigSharedPtr config, ThreadLocal::SlotAllocator& tls,
                   Random::RandomGenerator& random,
                   FluentdAccessLoggerCacheSharedPtr access_logger_cache);

private:
  /**
   * Per-thread cached logger.
   */
  struct ThreadLocalLogger : public ThreadLocal::ThreadLocalObject {
    ThreadLocalLogger(FluentdAccessLoggerSharedPtr logger) : logger_(std::move(logger)) {}

    const FluentdAccessLoggerSharedPtr logger_;
  };

  // Common::ImplBase
  void emitLog(const Formatter::HttpFormatterContext& context,
               const StreamInfo::StreamInfo& stream_info) override;

  FluentdFormatterPtr formatter_;
  const ThreadLocal::SlotPtr tls_slot_;
  const FluentdAccessLogConfigSharedPtr config_;
  const FluentdAccessLoggerCacheSharedPtr access_logger_cache_;
};

} // namespace Fluentd
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
