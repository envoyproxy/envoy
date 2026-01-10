#pragma once

#include <memory>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/config/core/v3/http_service.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
#include "envoy/local_info/local_info.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/http/async_client_impl.h"
#include "source/common/http/async_client_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/tracing/custom_tag_impl.h"
#include "source/extensions/access_loggers/common/access_log_base.h"
#include "source/extensions/access_loggers/open_telemetry/otlp_log_utils.h"
#include "source/extensions/access_loggers/open_telemetry/substitution_formatter.h"

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

/**
 * HTTP access logger that exports OTLP logs over HTTP.
 * Follows the same pattern as OpenTelemetryHttpTraceExporter.
 */
class HttpAccessLoggerImpl : public Logger::Loggable<Logger::Id::misc>,
                             public Http::AsyncClient::Callbacks {
public:
  HttpAccessLoggerImpl(
      Upstream::ClusterManager& cluster_manager,
      const envoy::config::core::v3::HttpService& http_service,
      const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
          config,
      Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info, Stats::Scope& scope);

  using SharedPtr = std::shared_ptr<HttpAccessLoggerImpl>;

  /**
   * Log a single log entry. Batches entries and flushes periodically.
   */
  void log(opentelemetry::proto::logs::v1::LogRecord&& entry);

  // Http::AsyncClient::Callbacks.
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override;
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override;
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

private:
  void flush();

  Upstream::ClusterManager& cluster_manager_;
  envoy::config::core::v3::HttpService http_service_;
  // Track active HTTP requests to be able to cancel them on destruction.
  Http::AsyncClientRequestTracker active_requests_;
  std::vector<std::pair<const Http::LowerCaseString, const std::string>> parsed_headers_to_add_;

  // Message structure: ExportLogsServiceRequest -> ResourceLogs -> ScopeLogs -> LogRecord.
  opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest message_;
  opentelemetry::proto::logs::v1::ScopeLogs* root_;

  // Batching timer.
  Event::TimerPtr flush_timer_;
  const std::chrono::milliseconds buffer_flush_interval_;
  const uint64_t max_buffer_size_bytes_;
  uint64_t approximate_message_size_bytes_ = 0;

  OtlpAccessLogStats stats_;
  uint32_t batched_log_entries_ = 0;
  uint32_t in_flight_log_entries_ = 0;
};

/**
 * Cache for HTTP access loggers. Creates one logger per unique configuration.
 */
class HttpAccessLoggerCacheImpl : public Singleton::Instance,
                                  public Logger::Loggable<Logger::Id::misc> {
public:
  HttpAccessLoggerCacheImpl(Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
                            ThreadLocal::SlotAllocator& tls,
                            const LocalInfo::LocalInfo& local_info);

  HttpAccessLoggerImpl::SharedPtr getOrCreateLogger(
      const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
          config,
      const envoy::config::core::v3::HttpService& http_service);

private:
  struct ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
    ThreadLocalCache(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}
    Event::Dispatcher& dispatcher_;
    absl::flat_hash_map<std::size_t, HttpAccessLoggerImpl::SharedPtr> access_loggers_;
  };

  Upstream::ClusterManager& cluster_manager_;
  Stats::Scope& scope_;
  ThreadLocal::SlotPtr tls_slot_;
  const LocalInfo::LocalInfo& local_info_;
};

using HttpAccessLoggerCacheSharedPtr = std::shared_ptr<HttpAccessLoggerCacheImpl>;

/**
 * Access log instance that streams logs over HTTP.
 */
class HttpAccessLog : public Common::ImplBase {
public:
  HttpAccessLog(
      ::Envoy::AccessLog::FilterPtr&& filter,
      envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config,
      ThreadLocal::SlotAllocator& tls, HttpAccessLoggerCacheSharedPtr access_logger_cache,
      const std::vector<Formatter::CommandParserPtr>& commands);

private:
  /**
   * Per-thread cached logger.
   */
  struct ThreadLocalLogger : public ThreadLocal::ThreadLocalObject {
    ThreadLocalLogger(HttpAccessLoggerImpl::SharedPtr logger);

    const HttpAccessLoggerImpl::SharedPtr logger_;
  };

  // Common::ImplBase
  void emitLog(const Formatter::Context& context, const StreamInfo::StreamInfo& info) override;

  const ThreadLocal::SlotPtr tls_slot_;
  const HttpAccessLoggerCacheSharedPtr access_logger_cache_;
  const envoy::config::core::v3::HttpService http_service_;
  std::unique_ptr<OpenTelemetryFormatter> body_formatter_;
  std::unique_ptr<OpenTelemetryFormatter> attributes_formatter_;
  const std::vector<std::string> filter_state_objects_to_log_;
  const std::vector<Tracing::CustomTagConstSharedPtr> custom_tags_;
};

using HttpAccessLogPtr = std::unique_ptr<HttpAccessLog>;

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
