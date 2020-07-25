#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

#include "common/grpc/typed_async_client.h"

#include "extensions/access_loggers/common/access_log_base.h"
#include "extensions/access_loggers/grpc/grpc_access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace HttpGrpc {

// TODO(mattklein123): Stats

/**
 * Access log Instance that streams HTTP logs over gRPC.
 */
class HttpGrpcAccessLog : public Common::ImplBase {
public:
  HttpGrpcAccessLog(AccessLog::FilterPtr&& filter,
                    envoy::extensions::access_loggers::grpc::v3::HttpGrpcAccessLogConfig config,
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
  const envoy::extensions::access_loggers::grpc::v3::HttpGrpcAccessLogConfig config_;
  const ThreadLocal::SlotPtr tls_slot_;
  const GrpcCommon::GrpcAccessLoggerCacheSharedPtr access_logger_cache_;
  std::vector<Http::LowerCaseString> request_headers_to_log_;
  std::vector<Http::LowerCaseString> response_headers_to_log_;
  std::vector<Http::LowerCaseString> response_trailers_to_log_;
  std::vector<std::string> filter_states_to_log_;
};

using HttpGrpcAccessLogPtr = std::unique_ptr<HttpGrpcAccessLog>;

} // namespace HttpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
