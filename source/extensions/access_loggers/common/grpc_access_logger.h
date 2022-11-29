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

template <typename LogRequest, typename LogResponse> class GrpcAccessLogClient {
public:
  GrpcAccessLogClient(const Grpc::RawAsyncClientSharedPtr& client,
                      const Protobuf::MethodDescriptor& service_method,
                      OptRef<const envoy::config::core::v3::RetryPolicy> retry_policy)
      : client_(client), service_method_(service_method),
        grpc_stream_retry_policy_(
            [retry_policy]() -> absl::optional<envoy::config::route::v3::RetryPolicy> {
              if (!retry_policy) {
                return absl::nullopt;
              }
              Http::Utility::validateCoreRetryPolicy(*retry_policy);
              const auto route_retry =
                  Http::Utility::convertCoreToRouteRetryPolicy(*retry_policy, "connect-failure");
              return absl::optional<envoy::config::route::v3::RetryPolicy>{route_retry};
            }()) {}

public:
  struct LocalStream : public Grpc::AsyncStreamCallbacks<LogResponse> {
    LocalStream(GrpcAccessLogClient& parent) : parent_(parent) {}

    // Grpc::AsyncStreamCallbacks
    void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
    void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
    void onReceiveMessage(std::unique_ptr<LogResponse>&&) override {}
    void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
    void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override {
      ASSERT(parent_.stream_ != nullptr);
      if (parent_.stream_->stream_ != nullptr) {
        // Only reset if we have a stream. Otherwise we had an inline failure and we will clear the
        // stream data in send().
        parent_.stream_.reset();
      }
    }

    GrpcAccessLogClient& parent_;
    Grpc::AsyncStream<LogRequest> stream_{};
  };

  bool isStreamStarted() { return stream_ != nullptr && stream_->stream_ != nullptr; }

  bool log(const LogRequest& request) {
    if (!stream_) {
      stream_ = std::make_unique<LocalStream>(*this);
    }

    if (stream_->stream_ == nullptr) {
      stream_->stream_ = client_->start(service_method_, *stream_, createStreamOptionsForRetry());
    }

    if (stream_->stream_ != nullptr) {
      if (stream_->stream_->isAboveWriteBufferHighWatermark()) {
        return false;
      }
      stream_->stream_->sendMessage(request, false);
    } else {
      // Clear out the stream data due to stream creation failure.
      stream_.reset();
    }
    return true;
  }

  Http::AsyncClient::StreamOptions createStreamOptionsForRetry() {
    auto opt = Http::AsyncClient::StreamOptions();

    if (!grpc_stream_retry_policy_) {
      return opt;
    }

    opt.setBufferBodyForRetry(true);
    opt.setRetryPolicy(*grpc_stream_retry_policy_);
    return opt;
  }

  Grpc::AsyncClient<LogRequest, LogResponse> client_;
  std::unique_ptr<LocalStream> stream_;
  const Protobuf::MethodDescriptor& service_method_;
  const absl::optional<envoy::config::route::v3::RetryPolicy> grpc_stream_retry_policy_;
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
      const Grpc::RawAsyncClientSharedPtr& client,
      const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
      Event::Dispatcher& dispatcher, Stats::Scope& scope, std::string access_log_prefix,
      const Protobuf::MethodDescriptor& service_method)
      : client_(client, service_method, GrpcCommon::optionalRetryPolicy(config)),
        buffer_flush_interval_msec_(
            PROTOBUF_GET_MS_OR_DEFAULT(config, buffer_flush_interval, 1000)),
        flush_timer_(dispatcher.createTimer([this]() {
          flush();
          flush_timer_->enableTimer(buffer_flush_interval_msec_);
        })),
        max_buffer_size_bytes_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, buffer_size_bytes, 16384)),
        stats_({ALL_GRPC_ACCESS_LOGGER_STATS(POOL_COUNTER_PREFIX(scope, access_log_prefix))}) {
    flush_timer_->enableTimer(buffer_flush_interval_msec_);
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
  Detail::GrpcAccessLogClient<LogRequest, LogResponse> client_;
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

    if (!client_.isStreamStarted()) {
      initMessage();
    }

    if (client_.log(message_)) {
      // Clear the message regardless of the success.
      approximate_message_size_bytes_ = 0;
      clearMessage();
    }
  }

  bool canLogMore() {
    if (max_buffer_size_bytes_ == 0 || approximate_message_size_bytes_ < max_buffer_size_bytes_) {
      stats_.logs_written_.inc();
      return true;
    }
    flush();
    if (approximate_message_size_bytes_ < max_buffer_size_bytes_) {
      stats_.logs_written_.inc();
      return true;
    }
    stats_.logs_dropped_.inc();
    return false;
  }

  const std::chrono::milliseconds buffer_flush_interval_msec_;
  const Event::TimerPtr flush_timer_;
  const uint64_t max_buffer_size_bytes_;
  uint64_t approximate_message_size_bytes_ = 0;
  GrpcAccessLoggerStats stats_;
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
