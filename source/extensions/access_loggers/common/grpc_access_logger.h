#pragma once

#include <bits/stdint-uintn.h>

#include <memory>
#include <string>

// TODO(shikugagwa): extensions should not be placed here
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/service/accesslog/v3/als.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/common/assert.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/protobuf/utility.h"

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
   * @param is_critical determine whether log entry is critical or not.
   */
  virtual void log(HttpLogProto&& entry, bool is_critical) PURE;

  /**
   * Log tcp access entry.
   * @param entry supplies the access log to send.
   * @param is_critical determine whether log entry is critical or not.
   */
  virtual void log(TcpLogProto&& entry, bool is_critical) PURE;
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
  getOrCreateLogger(const ConfigProto& config,
                    envoy::config::core::v3::ApiVersion transport_version,
                    GrpcAccessLoggerType logger_type, Stats::Scope& scope) PURE;
};

template <typename LogRequest, typename LogResponse> class GrpcAccessLogClient {
public:
  GrpcAccessLogClient(const Grpc::RawAsyncClientSharedPtr& client,
                      const Protobuf::MethodDescriptor& service_method)
      : GrpcAccessLogClient(client, service_method, absl::nullopt) {}
  GrpcAccessLogClient(const Grpc::RawAsyncClientSharedPtr& client,
                      const Protobuf::MethodDescriptor& service_method,
                      envoy::config::core::v3::ApiVersion transport_api_version)
      : client_(client), service_method_(service_method),
        transport_api_version_(transport_api_version) {}

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
      stream_->stream_ =
          client_->start(service_method_, *stream_, Http::AsyncClient::StreamOptions());
    }

    if (stream_->stream_ != nullptr) {
      if (stream_->stream_->isAboveWriteBufferHighWatermark()) {
        return false;
      }
      if (transport_api_version_.has_value()) {
        stream_->stream_->sendMessage(request, transport_api_version_.value(), false);
      } else {
        stream_->stream_->sendMessage(request, false);
      }
    } else {
      // Clear out the stream data due to stream creation failure.
      stream_.reset();
    }
    return true;
  }

  Grpc::AsyncClient<LogRequest, LogResponse> client_;
  std::unique_ptr<LocalStream> stream_;
  const Protobuf::MethodDescriptor& service_method_;
  const absl::optional<envoy::config::core::v3::ApiVersion> transport_api_version_;
};

template <class RequestType> class FatalAccessLoggerGrpcClient {
public:
  using ResponseType = envoy::service::accesslog::v3::BufferedCriticalAccessLogsResponse;

  FatalAccessLoggerGrpcClient(const Grpc::RawAsyncClientSharedPtr& client,
                              const Protobuf::MethodDescriptor& method)
      : FatalAccessLoggerGrpcClient(client, method, absl::nullopt) {}
  FatalAccessLoggerGrpcClient(
      const Grpc::RawAsyncClientSharedPtr& client, const Protobuf::MethodDescriptor& method,
      absl::optional<envoy::config::core::v3::ApiVersion> transport_api_version)
      : client_(client), service_method_(method), transport_api_version_(transport_api_version) {}

  struct BufferedMessage {
    enum class State {
      Initial,
      Pending,
    };

    State state_{State::Initial};
    ResponseType& message_;
  };

  struct ActiveStream : public Grpc::AsyncStreamCallbacks<ResponseType> {
    ActiveStream(FatalAccessLoggerGrpcClient<RequestType>& parent) : parent_(parent) {}

    // Grpc::AsyncStreamCallbacks
    void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
    void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
    void onReceiveMessage(std::unique_ptr<ResponseType>&& message) override {
      const auto& id = message->id();

      if (parent_.buffered_messages_.find(id) != parent_.buffered_messages_.end()) {
        switch (message->status()) {
        case envoy::service::accesslog::v3::BufferedCriticalAccessLogsResponse::ACK:
          parent_.buffered_messages_.erase(id);
          break;
        case envoy::service::accesslog::v3::BufferedCriticalAccessLogsResponse::NACK:
          parent_.buffered_messages_.at(id).state_ = BufferedMessage::State::Initial;
          // TODO(shikugawa): After many NACK occurred, Buffer will be overwhelmed.
          // To resolve this, Buffer should have size limit.
          break;
        default:
          return;
        }
      }
    }
    void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
    void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override {
      ASSERT(parent_.active_stream_ != nullptr);
      if (parent_.active_stream_->stream_ != nullptr) {
        parent_.active_stream_.reset();
      }
    }

    FatalAccessLoggerGrpcClient<RequestType>& parent_;
    Grpc::AsyncStream<RequestType> stream_;
  };

  void flush() {
    if (!active_stream_) {
      active_stream_ = std::make_unique<ActiveStream>(*this);
    }

    if (active_stream_->stream_ == nullptr) {
      active_stream_->stream_ =
          client_.start(service_method_, *active_stream_, Http::AsyncClient::StreamOptions());
    }

    if (active_stream_->stream_ == nullptr ||
        active_stream_->stream_.isAboveWriteBufferHighWatermark()) {
      active_stream_.reset();
      return;
    }

    for (auto&& buffered_message : buffered_messages_) {
      uint32_t id = buffered_message.first;
      buffered_message.second.message_.set_id(id);
      buffered_message.second.state_ = BufferedMessage::State::Pending;

      // TODO(shikugawa): stream must be ended with response withing specified timeout,
      // that must be defined to setup duration after last message transported.
      if (transport_api_version_.has_value()) {
        active_stream_->stream_->sendMessage(buffered_message.second.message_,
                                             transport_api_version_.value(), false);
      } else {
        active_stream_->stream_->sendMessage(buffered_message.second.message_, false);
      }
    }
  }

  bool isStreamStarted() { return active_stream_ != nullptr && active_stream_->stream_ != nullptr; }

private:
  friend ActiveStream;

  absl::flat_hash_map<uint32_t, BufferedMessage> buffered_messages_;
  std::unique_ptr<ActiveStream> active_stream_;
  Grpc::AsyncClient<RequestType, ResponseType> client_;
  const Protobuf::MethodDescriptor& service_method_;
  const absl::optional<envoy::config::core::v3::ApiVersion> transport_api_version_;
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

  GrpcAccessLogger(const Grpc::RawAsyncClientSharedPtr& client,
                   std::chrono::milliseconds buffer_flush_interval_msec,
                   uint64_t max_buffer_size_bytes, Event::Dispatcher& dispatcher,
                   Stats::Scope& scope, std::string access_log_prefix,
                   const Protobuf::MethodDescriptor& service_method)
      : GrpcAccessLogger(client, buffer_flush_interval_msec, max_buffer_size_bytes, dispatcher,
                         scope, access_log_prefix, service_method, absl::nullopt) {}
  GrpcAccessLogger(const Grpc::RawAsyncClientSharedPtr& client,
                   std::chrono::milliseconds buffer_flush_interval_msec,
                   uint64_t max_buffer_size_bytes, Event::Dispatcher& dispatcher,
                   Stats::Scope& scope, std::string access_log_prefix,
                   const Protobuf::MethodDescriptor& service_method,
                   envoy::config::core::v3::ApiVersion transport_api_version)
      : client_(client, service_method, transport_api_version),
        buffer_flush_interval_msec_(buffer_flush_interval_msec),
        flush_timer_(dispatcher.createTimer([this]() {
          flush();
          flushFatal();
          flush_timer_->enableTimer(buffer_flush_interval_msec_);
        })),
        max_buffer_size_bytes_(max_buffer_size_bytes),
        stats_({ALL_GRPC_ACCESS_LOGGER_STATS(POOL_COUNTER_PREFIX(scope, access_log_prefix))}) {
    flush_timer_->enableTimer(buffer_flush_interval_msec_);
  }

  void log(HttpLogProto&& entry, bool is_critical) {
    // TODO(shikugawa): If the log is critical, it will be stored in a message for critical log, but
    // for gRPC logger (e.g. OpenTelemetry) that does not support Critical Logging Endpoint, the
    // critical message will not be flushed forever In the case of a gRPC logger (e.g.
    // OpenTelemetry) that does not support critical logging endpoint, the critical message will not
    // be flushed forever. This can lead to a continual memory drain. Messages for critical logs may
    // need to be flushed at regular intervals in such cases.
    if (is_critical) {
      addFatalEntry(std::move(entry));
      return;
    }

    if (!canLogMore()) {
      return;
    }
    approximate_message_size_bytes_ += entry.ByteSizeLong();
    addEntry(std::move(entry));
    if (approximate_message_size_bytes_ >= max_buffer_size_bytes_) {
      flush();
    }
  }

  void log(TcpLogProto&& entry, bool is_critical) {
    if (is_critical) {
      addFatalEntry(std::move(entry));
      return;
    }

    approximate_message_size_bytes_ += entry.ByteSizeLong();
    addEntry(std::move(entry));
    if (approximate_message_size_bytes_ >= max_buffer_size_bytes_) {
      flush();
    }
  }

protected:
  void initFatalLoggerClient(const Grpc::RawAsyncClientSharedPtr& client,
                             const Protobuf::MethodDescriptor& service_method,
                             envoy::config::core::v3::ApiVersion transport_api_version) {
    fatal_client_ = std::make_unique<Detail::FatalAccessLoggerGrpcClient<LogRequest>>(
        client, service_method, transport_api_version);
  }

  Detail::GrpcAccessLogClient<LogRequest, LogResponse> client_;
  std::unique_ptr<Detail::FatalAccessLoggerGrpcClient<LogRequest>> fatal_client_;
  LogRequest message_;
  LogRequest fatal_message_;

private:
  virtual bool isEmpty() PURE;
  virtual bool isFatalEmpty() PURE;
  virtual void initMessage() PURE;
  virtual void initFatalMessage() PURE;
  virtual void addEntry(HttpLogProto&& entry) PURE;
  virtual void addEntry(TcpLogProto&& entry) PURE;
  virtual void addFatalEntry(HttpLogProto&& entry) PURE;
  virtual void addFatalEntry(TcpLogProto&& entry) PURE;
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

  void flushFatal() {
    if (isFatalEmpty() || fatal_client_ == nullptr) {
      return;
    }
    if (fatal_client_->isStreamStarted()) {
      initFatalMessage();
    }

    fatal_client_->flush();
    // TODO(shikugawa): consider message and buffer lifecycle.
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
      : async_client_manager_(async_client_manager), scope_(scope), tls_slot_(tls.allocateSlot()) {
    tls_slot_->set([](Event::Dispatcher& dispatcher) {
      return std::make_shared<ThreadLocalCache>(dispatcher);
    });
  }

  typename GrpcAccessLogger::SharedPtr
  getOrCreateLogger(const ConfigProto& config,
                    envoy::config::core::v3::ApiVersion transport_version,
                    GrpcAccessLoggerType logger_type, Stats::Scope& scope) override {
    // TODO(euroelessar): Consider cleaning up loggers.
    auto& cache = tls_slot_->getTyped<ThreadLocalCache>();
    const auto cache_key = std::make_pair(MessageUtil::hash(config), logger_type);
    const auto it = cache.access_loggers_.find(cache_key);
    if (it != cache.access_loggers_.end()) {
      return it->second;
    }
    const auto logger = createLogger(
        config, transport_version,
        async_client_manager_.factoryForGrpcService(config.grpc_service(), scope_, false)
            ->createUncachedRawAsyncClient(),
        std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config, buffer_flush_interval, 1000)),
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, buffer_size_bytes, 16384), cache.dispatcher_,
        scope);
    cache.access_loggers_.emplace(cache_key, logger);
    return logger;
  }

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
  virtual typename GrpcAccessLogger::SharedPtr
  createLogger(const ConfigProto& config, envoy::config::core::v3::ApiVersion transport_version,
               const Grpc::RawAsyncClientSharedPtr& client,
               std::chrono::milliseconds buffer_flush_interval_msec, uint64_t max_buffer_size_bytes,
               Event::Dispatcher& dispatcher, Stats::Scope& scope) PURE;

  Grpc::AsyncClientManager& async_client_manager_;
  Stats::Scope& scope_;
  ThreadLocal::SlotPtr tls_slot_;
};

} // namespace Common
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
