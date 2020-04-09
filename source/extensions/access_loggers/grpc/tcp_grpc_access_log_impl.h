#pragma once

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
namespace TcpGrpc {

// TODO(mattklein123): Stats

/**
 * Access log Instance that streams TCP logs over gRPC.
 */
class TcpGrpcAccessLog : public Common::ImplBase {
public:
  TcpGrpcAccessLog(AccessLog::FilterPtr&& filter,
                   envoy::extensions::access_loggers::grpc::v3::TcpGrpcAccessLogConfig config,
                   ThreadLocal::SlotAllocator& tls,
                   GrpcCommon::GrpcAccessLoggerCacheSharedPtr access_logger_cache);

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

  const envoy::extensions::access_loggers::grpc::v3::TcpGrpcAccessLogConfig config_;
  const ThreadLocal::SlotPtr tls_slot_;
  const GrpcCommon::GrpcAccessLoggerCacheSharedPtr access_logger_cache_;
};

} // namespace TcpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
