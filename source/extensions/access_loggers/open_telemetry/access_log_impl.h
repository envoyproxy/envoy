#pragma once

#include <memory>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/common/tracing/custom_tag_impl.h"
#include "source/extensions/access_loggers/common/access_log_base.h"
#include "source/extensions/access_loggers/open_telemetry/grpc_access_log_impl.h"
#include "source/extensions/access_loggers/open_telemetry/substitution_formatter.h"

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
  AccessLog(
      ::Envoy::AccessLog::FilterPtr&& filter,
      envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config,
      ThreadLocal::SlotAllocator& tls, GrpcAccessLoggerCacheSharedPtr access_logger_cache,
      const std::vector<Formatter::CommandParserPtr>& commands);

private:
  /**
   * Per-thread cached logger.
   */
  struct ThreadLocalLogger : public ThreadLocal::ThreadLocalObject {
    ThreadLocalLogger(GrpcAccessLoggerSharedPtr logger);

    const GrpcAccessLoggerSharedPtr logger_;
  };

  // Common::ImplBase
  void emitLog(const Formatter::Context& context, const StreamInfo::StreamInfo& info) override;

  const ThreadLocal::SlotPtr tls_slot_;
  const GrpcAccessLoggerCacheSharedPtr access_logger_cache_;
  std::unique_ptr<OpenTelemetryFormatter> body_formatter_;
  std::unique_ptr<OpenTelemetryFormatter> attributes_formatter_;
  const std::vector<std::string> filter_state_objects_to_log_;
  const std::vector<Tracing::CustomTagConstSharedPtr> custom_tags_;
};

using AccessLogPtr = std::unique_ptr<AccessLog>;

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
