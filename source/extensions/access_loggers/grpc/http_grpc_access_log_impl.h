#pragma once

#include <memory>
#include <vector>

#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/access_loggers/common/access_log_base.h"
#include "source/extensions/access_loggers/grpc/grpc_access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace HttpGrpc {

// TODO(mattklein123): Stats

using envoy::extensions::access_loggers::grpc::v3::HttpGrpcAccessLogConfig;
using HttpGrpcAccessLogConfigConstSharedPtr = std::shared_ptr<const HttpGrpcAccessLogConfig>;

/**
 * Access log Instance that streams HTTP logs over gRPC.
 */
class HttpGrpcAccessLog : public Common::ImplBase {
public:
  HttpGrpcAccessLog(AccessLog::FilterPtr&& filter, const HttpGrpcAccessLogConfig config,
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
  void emitLog(const Formatter::HttpFormatterContext& context,
               const StreamInfo::StreamInfo& info) override;

  const HttpGrpcAccessLogConfigConstSharedPtr config_;
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
