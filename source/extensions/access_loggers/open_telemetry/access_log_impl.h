#pragma once

#include <memory>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3alpha/logs_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/formatter/substitution_formatter.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/access_loggers/common/access_log_base.h"
#include "source/extensions/access_loggers/open_telemetry/grpc_access_log_impl.h"

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

// TODO(mattklein123): Stats

/**
 * Access log Instance that streams logs over gRPC.
 */
class AccessLog : public Common::ImplBase {
public:
  AccessLog(::Envoy::AccessLog::FilterPtr&& filter,
            envoy::extensions::access_loggers::open_telemetry::v3alpha::OpenTelemetryAccessLogConfig
                config,
            ThreadLocal::SlotAllocator& tls, GrpcAccessLoggerCacheSharedPtr access_logger_cache,
            Stats::Scope& scope);

private:
  /**
   * Per-thread cached logger.
   */
  struct ThreadLocalLogger : public ThreadLocal::ThreadLocalObject {
    ThreadLocalLogger(GrpcAccessLoggerSharedPtr logger);

    const GrpcAccessLoggerSharedPtr logger_;
  };

  // Common::ImplBase
  void emitLog(const Http::RequestHeaderMap& request_headers,
               const Http::ResponseHeaderMap& response_headers,
               const Http::ResponseTrailerMap& response_trailers,
               const StreamInfo::StreamInfo& stream_info) override;

  Stats::Scope& scope_;
  const ThreadLocal::SlotPtr tls_slot_;
  const GrpcAccessLoggerCacheSharedPtr access_logger_cache_;
  std::unique_ptr<Formatter::StructFormatter> body_formatter_;
  std::unique_ptr<Formatter::StructFormatter> attributes_formatter_;
};

using AccessLogPtr = std::unique_ptr<AccessLog>;

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
