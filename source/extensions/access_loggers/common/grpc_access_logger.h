#pragma once

#include <memory>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/assert.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/access_loggers/common/grpc_access_logger_clients.h"
#include "source/extensions/access_loggers/common/grpc_access_logger_utils.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Common {

enum class GrpcAccessLoggerType { TCP, HTTP };

namespace Detail {

/**
 * Fully specialized types of the interfaces below are available through the
 * `Common::GrpcAccessLogger::Interface` and `Common::GrpcAccessLoggerCache::interface`
 * aliases.
 */

/**
 * Interface for an access logger. The logger provides abstraction on top of gRPC stream, deals with
 * reconnects and performs batching.
 */
template <typename HttpLogProto, typename TcpLogProto> class GrpcAccessLogger {
public:
  using SharedPtr = std::shared_ptr<GrpcAccessLogger>;

  virtual ~GrpcAccessLogger() = default;

  /**
   * Log http access entry.
   * @param entry supplies the access log to send.
   */
  virtual void log(HttpLogProto&& entry) PURE;

  /**
   * Log tcp access entry.
   * @param entry supplies the access log to send.
   */
  virtual void log(TcpLogProto&& entry) PURE;
};

/**
 * Interface for an access logger cache. The cache deals with threading and de-duplicates loggers
 * for the same configuration.
 */
template <typename GrpcAccessLogger, typename ConfigProto> class GrpcAccessLoggerCache {
public:
  using SharedPtr = std::shared_ptr<GrpcAccessLoggerCache>;
  virtual ~GrpcAccessLoggerCache() = default;

  /**
   * Get existing logger or create a new one for the given configuration.
   * @param config supplies the configuration for the logger.
   * @return GrpcAccessLoggerSharedPtr ready for logging requests.
   */
  virtual typename GrpcAccessLogger::SharedPtr
  getOrCreateLogger(const ConfigProto& config, GrpcAccessLoggerType logger_type) PURE;
};

} // namespace Detail

/**
 * All stats for the grpc access logger. @see stats_macros.h
 */
#define ALL_GRPC_ACCESS_LOGGER_STATS(COUNTER)                                                      \
  COUNTER(logs_written)                                                                            \
  COUNTER(logs_dropped)

/**
 * Wrapper struct for the access log stats. @see stats_macros.h
 */
struct GrpcAccessLoggerStats {
  ALL_GRPC_ACCESS_LOGGER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Base class for defining a gRPC logger with the `HttpLogProto` and `TcpLogProto` access log
 * entries and `LogRequest` and `LogResponse` gRPC messages.
 * The log entries and messages are distinct types to support batching of multiple access log
 * entries in a single gRPC messages that go on the wire.
 */
template <typename HttpLogProto, typename TcpLogProto, typename LogRequest, typename LogResponse>
class GrpcAccessLogger : public Detail::GrpcAccessLogger<HttpLogProto, TcpLogProto> {
public:
  using Interface = Detail::GrpcAccessLogger<HttpLogProto, TcpLogProto>;
  GrpcAccessLogger(
      const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
      Event::Dispatcher& dispatcher, Stats::Scope& scope,
      absl::optional<std::string> access_log_prefix,
      std::unique_ptr<GrpcAccessLogClient<LogRequest, LogResponse>> client)
      : client_(std::move(client)), buffer_flush_interval_msec_(PROTOBUF_GET_MS_OR_DEFAULT(
                                        config, buffer_flush_interval, 1000)),
        flush_timer_(dispatcher.createTimer([this]() {
          flush();
          flush_timer_->enableTimer(buffer_flush_interval_msec_);
        })),
        max_buffer_size_bytes_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, buffer_size_bytes, 16384)) {
    flush_timer_->enableTimer(buffer_flush_interval_msec_);
    if (access_log_prefix.has_value()) {
      stats_ = std::make_unique<GrpcAccessLoggerStats>(GrpcAccessLoggerStats{
          ALL_GRPC_ACCESS_LOGGER_STATS(POOL_COUNTER_PREFIX(scope, access_log_prefix.value()))});
    }
  }

  void log(HttpLogProto&& entry) override {
    if (!canLogMore()) {
      return;
    }
    approximate_message_size_bytes_ += entry.ByteSizeLong();
    addEntry(std::move(entry));
    if (approximate_message_size_bytes_ >= max_buffer_size_bytes_) {
      flush();
    }
  }

  void log(TcpLogProto&& entry) override {
    approximate_message_size_bytes_ += entry.ByteSizeLong();
    addEntry(std::move(entry));
    if (approximate_message_size_bytes_ >= max_buffer_size_bytes_) {
      flush();
    }
  }

protected:
  std::unique_ptr<GrpcAccessLogClient<LogRequest, LogResponse>> client_;
  LogRequest message_;

private:
  virtual bool isEmpty() PURE;
  virtual void initMessage() PURE;
  virtual void addEntry(HttpLogProto&& entry) PURE;
  virtual void addEntry(TcpLogProto&& entry) PURE;
  virtual void clearMessage() { message_.Clear(); }

  void flush() {
    if (isEmpty()) {
      // Nothing to flush.
      return;
    }

    if (!client_->isConnected()) {
      initMessage();
    }

    if (client_->log(message_)) {
      // Clear the message regardless of the success.
      approximate_message_size_bytes_ = 0;
      clearMessage();
    }
  }

  // `canLogMore()` is only for streaming gRPC client only which could run into
  // AboveWriteBufferHighWatermark during `flush()` so that we tracks the log entries dropped caused
  // by that.
  //
  // For unary gRPC client, `canLogMore` always returns True[1][2] so `stats_` here is meaningless.
  //
  // [1]https://github.com/envoyproxy/envoy/blob/cd5ef906026160ec2cd766d8d18217e668c256d8/source/extensions/access_loggers/common/grpc_access_logger.h#L287.
  // [2]https://github.com/envoyproxy/envoy/blob/cd5ef906026160ec2cd766d8d18217e668c256d8/source/extensions/access_loggers/common/grpc_access_logger.h#L126
  bool canLogMore() {
    if (max_buffer_size_bytes_ == 0 || approximate_message_size_bytes_ < max_buffer_size_bytes_) {
      incLogsWrittenStats();
      return true;
    }
    flush();
    if (approximate_message_size_bytes_ < max_buffer_size_bytes_) {
      incLogsWrittenStats();
      return true;
    }
    incLogsDroppedStats();
    return false;
  }

  void incLogsDroppedStats() {
    if (stats_) {
      stats_->logs_dropped_.inc();
    }
  }

  void incLogsWrittenStats() {
    if (stats_) {
      stats_->logs_written_.inc();
    }
  }

  const std::chrono::milliseconds buffer_flush_interval_msec_;
  const Event::TimerPtr flush_timer_;
  const uint64_t max_buffer_size_bytes_;
  uint64_t approximate_message_size_bytes_ = 0;
  std::unique_ptr<GrpcAccessLoggerStats> stats_ = nullptr;
};

/**
 * Class for defining logger cache with the `GrpcAccessLogger` interface and
 * `ConfigProto` configuration.
 */
template <typename GrpcAccessLogger, typename ConfigProto>
class GrpcAccessLoggerCache : public Singleton::Instance,
                              public Detail::GrpcAccessLoggerCache<GrpcAccessLogger, ConfigProto> {
public:
  using Interface = Detail::GrpcAccessLoggerCache<GrpcAccessLogger, ConfigProto>;

  GrpcAccessLoggerCache(Grpc::AsyncClientManager& async_client_manager, Stats::Scope& scope,
                        ThreadLocal::SlotAllocator& tls)
      : scope_(scope), async_client_manager_(async_client_manager), tls_slot_(tls.allocateSlot()) {
    tls_slot_->set([](Event::Dispatcher& dispatcher) {
      return std::make_shared<ThreadLocalCache>(dispatcher);
    });
  }

  typename GrpcAccessLogger::SharedPtr
  getOrCreateLogger(const ConfigProto& config, GrpcAccessLoggerType logger_type) override {
    // TODO(euroelessar): Consider cleaning up loggers.
    auto& cache = tls_slot_->getTyped<ThreadLocalCache>();
    const auto cache_key = std::make_pair(MessageUtil::hash(config), logger_type);
    const auto it = cache.access_loggers_.find(cache_key);
    if (it != cache.access_loggers_.end()) {
      return it->second;
    }

    const auto logger = createLogger(config, cache.dispatcher_);
    cache.access_loggers_.emplace(cache_key, logger);
    return logger;
  }

protected:
  Stats::Scope& scope_;
  Grpc::AsyncClientManager& async_client_manager_;

private:
  /**
   * Per-thread cache.
   */
  struct ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
    ThreadLocalCache(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

    Event::Dispatcher& dispatcher_;
    // Access loggers indexed by the hash of logger's configuration and logger type.
    absl::flat_hash_map<std::pair<std::size_t, Common::GrpcAccessLoggerType>,
                        typename GrpcAccessLogger::SharedPtr>
        access_loggers_;
  };

  // Create the specific logger type for this cache.
  virtual typename GrpcAccessLogger::SharedPtr createLogger(const ConfigProto& config,
                                                            Event::Dispatcher& dispatcher) PURE;

  ThreadLocal::SlotPtr tls_slot_;
};

} // namespace Common
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
