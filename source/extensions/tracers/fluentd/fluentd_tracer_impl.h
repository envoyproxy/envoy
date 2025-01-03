#pragma once

#include <chrono>

#include "envoy/config/trace/v3/fluentd.pb.h"
#include "envoy/config/trace/v3/fluentd.pb.validate.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/trace_driver.h"

#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/tracing/trace_context_impl.h"
#include "source/extensions/tracers/common/factory_base.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Fluentd {

using FluentdConfig = envoy::config::trace::v3::FluentdConfig;
using FluentdConfigSharedPtr = std::shared_ptr<FluentdConfig>;

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
  Entry(uint64_t time, std::map<std::string, std::string>&& record)
      : time_(time), record_(std::move(record)) {}

  const uint64_t time_;
  const std::map<std::string, std::string> record_;
};

using EntryPtr = std::unique_ptr<Entry>;

// Span context definitions
class SpanContext {
public:
  SpanContext() = default;
  SpanContext(const absl::string_view& version, const absl::string_view& trace_id,
              const absl::string_view& parent_id, bool sampled, const absl::string_view& tracestate)
      : version_(version), trace_id_(trace_id), parent_id_(parent_id), sampled_(sampled),
        tracestate_(tracestate) {}

  const std::string& version() const { return version_; }

  const std::string& traceId() const { return trace_id_; }

  const std::string& parentId() const { return parent_id_; }

  bool sampled() const { return sampled_; }

  const std::string& tracestate() const { return tracestate_; }

private:
  const std::string version_;
  const std::string trace_id_;
  const std::string parent_id_;
  const bool sampled_{false};
  const std::string tracestate_;
};

// Trace context definitions
class FluentdConstantValues {
public:
  const Tracing::TraceContextHandler TRACE_PARENT{"traceparent"};
  const Tracing::TraceContextHandler TRACE_STATE{"tracestate"};
};

using FluentdConstants = ConstSingleton<FluentdConstantValues>;

// SpanContextExtractor extracts the span context from the trace context
class SpanContextExtractor {
public:
  SpanContextExtractor(Tracing::TraceContext& trace_context);
  ~SpanContextExtractor();
  absl::StatusOr<SpanContext> extractSpanContext();
  bool propagationHeaderPresent();

private:
  const Tracing::TraceContext& trace_context_;
};

// Template for a Fluentd tracer
class FluentdTracer {
public:
  virtual ~FluentdTracer() = default;

  /**
   * Send the Fluentd formatted message over the upstream TCP connection.
   */
  virtual void trace(EntryPtr&& entry) PURE;
};

// Fluentd tracer stats
#define TRACER_FLUENTD_STATS(COUNTER, GAUGE)                                                       \
  COUNTER(entries_lost)                                                                            \
  COUNTER(entries_buffered)                                                                        \
  COUNTER(events_sent)                                                                             \
  COUNTER(reconnect_attempts)                                                                      \
  COUNTER(connections_closed)

struct TracerFluentdStats {
  TRACER_FLUENTD_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

// FluentdTracerImpl implements a FluentdTracer, handling tracing and buffer/connection logic
class FluentdTracerImpl : public Tcp::AsyncTcpClientCallbacks,
                          public FluentdTracer,
                          public std::enable_shared_from_this<FluentdTracerImpl>,
                          public Logger::Loggable<Logger::Id::tracing> {
public:
  FluentdTracerImpl(Upstream::ThreadLocalCluster& cluster, Tcp::AsyncTcpClientPtr client,
                    Event::Dispatcher& dispatcher, const FluentdConfig& config,
                    BackOffStrategyPtr backoff_strategy, Stats::Scope& parent_scope,
                    Random::RandomGenerator& random, TimeSource& time_source);

  // Tcp::AsyncTcpClientCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}
  void onData(Buffer::Instance&, bool) override {}

  Tracing::SpanPtr startSpan(Tracing::TraceContext& trace_context, SystemTime start_time,
                             const std::string& operation_name, Tracing::Decision tracing_decision);

  Tracing::SpanPtr startSpan(Tracing::TraceContext& trace_context, SystemTime start_time,
                             const std::string& operation_name, Tracing::Decision tracing_decision,
                             const SpanContext& previous_span_context);

  // FluentdTracer
  void trace(EntryPtr&& entry) override;

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
  TracerFluentdStats fluentd_stats_;
  std::vector<EntryPtr> entries_;
  uint64_t approximate_message_size_bytes_ = 0;
  Upstream::ThreadLocalCluster& cluster_;
  const BackOffStrategyPtr backoff_strategy_;
  const Tcp::AsyncTcpClientPtr client_;
  const std::chrono::milliseconds buffer_flush_interval_msec_;
  const uint64_t max_buffer_size_bytes_;
  const Event::TimerPtr retry_timer_;
  const Event::TimerPtr flush_timer_;
  std::map<std::string, std::string> option_;
  Random::RandomGenerator& random_;
  TimeSource& time_source_;
};

using FluentdTracerWeakPtr = std::weak_ptr<FluentdTracerImpl>;
using FluentdTracerSharedPtr = std::shared_ptr<FluentdTracerImpl>;

// FluentdTracerCache is used to cache entries before they are sent to the Fluentd server
class FluentdTracerCache {
public:
  virtual ~FluentdTracerCache() = default;

  /**
   * Get existing tracer or create a new one for the given configuration.
   * @return FluentdTracerSharedPtr ready for logging requests.
   */
  virtual FluentdTracerSharedPtr getOrCreateTracer(const FluentdConfigSharedPtr config,
                                                   Random::RandomGenerator& random,
                                                   TimeSource& time_source) PURE;
};

using FluentdTracerCacheSharedPtr = std::shared_ptr<FluentdTracerCache>;

// Implementation of the FluentdTracerCache as a thread-local cache
class FluentdTracerCacheImpl : public Singleton::Instance, public FluentdTracerCache {
public:
  FluentdTracerCacheImpl(Upstream::ClusterManager& cluster_manager, Stats::Scope& parent_scope,
                         ThreadLocal::SlotAllocator& tls);

  // FluentdTracerCache
  FluentdTracerSharedPtr getOrCreateTracer(const FluentdConfigSharedPtr config,
                                           Random::RandomGenerator& random,
                                           TimeSource& time_source) override;

private:
  /**
   * Per-thread cache.
   */
  struct ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
    ThreadLocalCache(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

    Event::Dispatcher& dispatcher_;
    // Tracers indexed by the hash of tracer's configuration.
    absl::flat_hash_map<std::size_t, FluentdTracerWeakPtr> tracers_;
  };

  Upstream::ClusterManager& cluster_manager_;
  Stats::ScopeSharedPtr stats_scope_;
  ThreadLocal::SlotPtr tls_slot_;
};

using TracerPtr = std::unique_ptr<FluentdTracerImpl>;

// Driver manages and creates Fluentd tracers
class Driver : Logger::Loggable<Logger::Id::tracing>, public Tracing::Driver {
public:
  Driver(const FluentdConfigSharedPtr fluentd_config,
         Server::Configuration::TracerFactoryContext& context,
         FluentdTracerCacheSharedPtr tracer_cache);

  // Tracing::Driver
  Tracing::SpanPtr startSpan(const Tracing::Config& config, Tracing::TraceContext& trace_context,
                             const StreamInfo::StreamInfo& stream_info,
                             const std::string& operation_name,
                             Tracing::Decision tracing_decision) override;

private:
  class ThreadLocalTracer : public ThreadLocal::ThreadLocalObject {
  public:
    ThreadLocalTracer(FluentdTracerSharedPtr tracer) : tracer_(std::move(tracer)) {}

    FluentdTracerImpl& tracer() { return *tracer_; }

    FluentdTracerSharedPtr tracer_;
  };

private:
  ThreadLocal::SlotPtr tls_slot_;
  const FluentdConfigSharedPtr fluentd_config_;
  FluentdTracerCacheSharedPtr tracer_cache_;
};

// Span holds the span context and handles span operations
class Span : public Tracing::Span {
public:
  Span(Tracing::TraceContext& trace_context, SystemTime start_time,
       const std::string& operation_name, Tracing::Decision tracing_decision,
       FluentdTracerSharedPtr tracer, const SpanContext& span_context, TimeSource& time_source);

  // Tracing::Span
  void setOperation(absl::string_view operation) override;
  void setTag(absl::string_view name, absl::string_view value) override;
  void log(SystemTime timestamp, const std::string& event) override;
  void finishSpan() override;
  void injectContext(Tracing::TraceContext& trace_context,
                     const Tracing::UpstreamContext& upstream) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;
  void setSampled(bool sampled) override;
  bool sampled() const { return sampled_; }
  std::string getBaggage(absl::string_view key) override;
  void setBaggage(absl::string_view key, absl::string_view value) override;
  std::string getTraceId() const override;
  std::string getSpanId() const override;

private:
  // config
  Tracing::TraceContext& trace_context_;
  SystemTime start_time_;
  std::string operation_;
  Tracing::Decision tracing_decision_;

  FluentdTracerSharedPtr tracer_;
  SpanContext span_context_;
  std::map<std::string, std::string> tags_;
  bool sampled_;
  TimeSource& time_source_;
};

} // namespace Fluentd
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
