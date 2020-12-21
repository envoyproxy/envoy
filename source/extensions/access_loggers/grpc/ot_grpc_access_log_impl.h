#pragma once

#include <memory>
#include <vector>

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

#include "common/grpc/typed_async_client.h"

#include "extensions/access_loggers/common/access_log_base.h"
#include "extensions/access_loggers/grpc/grpc_ot_access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace HttpGrpc {

// TODO(mattklein123): Stats

/**
 * Access log Instance that streams HTTP logs over gRPC.
 */
class OtGrpcAccessLog : public Common::ImplBase {
public:
  OtGrpcAccessLog(AccessLog::FilterPtr&& filter,
                  envoy::extensions::access_loggers::grpc::v3::OtGrpcAccessLogConfig config,
                  ThreadLocal::SlotAllocator& tls,
                  GrpcCommon::GrpcAccessLoggerCacheSharedPtr access_logger_cache,
                  Stats::Scope& scope);

private:
  /**
   * Per-thread cached logger.
   */
  struct ThreadLocalLogger : public ThreadLocal::ThreadLocalObject {
    ThreadLocalLogger(GrpcCommon::GrpcAccessLoggerSharedPtr logger);

    const GrpcCommon::GrpcAccessLoggerSharedPtr logger_;
  };

  // Common::ImplBase
  void emitLog(const Http::RequestHeaderMap& request_headers,
               const Http::ResponseHeaderMap& response_headers,
               const Http::ResponseTrailerMap& response_trailers,
               const StreamInfo::StreamInfo& stream_info) override;

  Stats::Scope& scope_;
  const envoy::extensions::access_loggers::grpc::v3::OtGrpcAccessLogConfig config_;
  const ThreadLocal::SlotPtr tls_slot_;
  const GrpcCommon::GrpcAccessLoggerCacheSharedPtr access_logger_cache_;
  std::vector<Http::LowerCaseString> request_headers_to_log_;
  std::vector<Http::LowerCaseString> response_headers_to_log_;
  std::vector<Http::LowerCaseString> response_trailers_to_log_;
  std::vector<std::string> filter_states_to_log_;
  const StructFormatter formatter_;
};

using OtGrpcAccessLogPtr = std::unique_ptr<OtGrpcAccessLog>;

} // namespace HttpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
