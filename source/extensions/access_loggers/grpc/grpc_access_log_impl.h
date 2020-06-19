#pragma once

#include <unordered_map>
#include <vector>

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/service/accesslog/v3/als.pb.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

#include "common/grpc/typed_async_client.h"

#include "extensions/access_loggers/common/access_log_base.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

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
 * Interface for an access logger. The logger provides abstraction on top of gRPC stream, deals with
 * reconnects and performs batching.
 */
class GrpcAccessLogger {
public:
  virtual ~GrpcAccessLogger() = default;

  /**
   * Log http access entry.
   * @param entry supplies the access log to send.
   */
  virtual void log(envoy::data::accesslog::v3::HTTPAccessLogEntry&& entry) PURE;

  /**
   * Log tcp access entry.
   * @param entry supplies the access log to send.
   */
  virtual void log(envoy::data::accesslog::v3::TCPAccessLogEntry&& entry) PURE;
};

using GrpcAccessLoggerSharedPtr = std::shared_ptr<GrpcAccessLogger>;

enum class GrpcAccessLoggerType { TCP, HTTP };

/**
 * Interface for an access logger cache. The cache deals with threading and de-duplicates loggers
 * for the same configuration.
 */
class GrpcAccessLoggerCache {
public:
  virtual ~GrpcAccessLoggerCache() = default;

  /**
   * Get existing logger or create a new one for the given configuration.
   * @param config supplies the configuration for the logger.
   * @return GrpcAccessLoggerSharedPtr ready for logging requests.
   */
  virtual GrpcAccessLoggerSharedPtr getOrCreateLogger(
      const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
      GrpcAccessLoggerType logger_type, Stats::Scope& scope) PURE;
};

using GrpcAccessLoggerCacheSharedPtr = std::shared_ptr<GrpcAccessLoggerCache>;

class GrpcAccessLoggerImpl : public GrpcAccessLogger {
public:
  GrpcAccessLoggerImpl(Grpc::RawAsyncClientPtr&& client, std::string log_name,
                       std::chrono::milliseconds buffer_flush_interval_msec,
                       uint64_t max_buffer_size_bytes, Event::Dispatcher& dispatcher,
                       const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
                       envoy::config::core::v3::ApiVersion transport_api_version);

  // Extensions::AccessLoggers::GrpcCommon::GrpcAccessLogger
  void log(envoy::data::accesslog::v3::HTTPAccessLogEntry&& entry) override;
  void log(envoy::data::accesslog::v3::TCPAccessLogEntry&& entry) override;

private:
  struct LocalStream
      : public Grpc::AsyncStreamCallbacks<envoy::service::accesslog::v3::StreamAccessLogsResponse> {
    LocalStream(GrpcAccessLoggerImpl& parent) : parent_(parent) {}

    // Grpc::AsyncStreamCallbacks
    void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
    void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
    void onReceiveMessage(
        std::unique_ptr<envoy::service::accesslog::v3::StreamAccessLogsResponse>&&) override {}
    void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
    void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

    GrpcAccessLoggerImpl& parent_;
    Grpc::AsyncStream<envoy::service::accesslog::v3::StreamAccessLogsMessage> stream_{};
  };

  void flush();

  bool canLogMore();

  GrpcAccessLoggerStats stats_;
  Grpc::AsyncClient<envoy::service::accesslog::v3::StreamAccessLogsMessage,
                    envoy::service::accesslog::v3::StreamAccessLogsResponse>
      client_;
  const std::string log_name_;
  const std::chrono::milliseconds buffer_flush_interval_msec_;
  const Event::TimerPtr flush_timer_;
  const uint64_t max_buffer_size_bytes_;
  uint64_t approximate_message_size_bytes_ = 0;
  envoy::service::accesslog::v3::StreamAccessLogsMessage message_;
  absl::optional<LocalStream> stream_;
  const LocalInfo::LocalInfo& local_info_;
  const Protobuf::MethodDescriptor& service_method_;
  const envoy::config::core::v3::ApiVersion transport_api_version_;
};

class GrpcAccessLoggerCacheImpl : public Singleton::Instance, public GrpcAccessLoggerCache {
public:
  GrpcAccessLoggerCacheImpl(Grpc::AsyncClientManager& async_client_manager, Stats::Scope& scope,
                            ThreadLocal::SlotAllocator& tls,
                            const LocalInfo::LocalInfo& local_info);

  GrpcAccessLoggerSharedPtr getOrCreateLogger(
      const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
      GrpcAccessLoggerType logger_type, Stats::Scope& scope) override;

private:
  /**
   * Per-thread cache.
   */
  struct ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
    ThreadLocalCache(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

    Event::Dispatcher& dispatcher_;
    // Access loggers indexed by the hash of logger's configuration and logger type.
    absl::flat_hash_map<std::pair<std::size_t, GrpcAccessLoggerType>, GrpcAccessLoggerSharedPtr>
        access_loggers_;
  };

  Grpc::AsyncClientManager& async_client_manager_;
  Stats::Scope& scope_;
  ThreadLocal::SlotPtr tls_slot_;
  const LocalInfo::LocalInfo& local_info_;
};

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
