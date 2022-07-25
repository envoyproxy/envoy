#include "source/extensions/access_loggers/common/grpc_access_logger_utils.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

OptRef<const envoy::config::core::v3::RetryPolicy> optionalRetryPolicy(
    const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config) {
  if (!config.has_grpc_stream_retry_policy()) {
    return {};
  }
  return config.grpc_stream_retry_policy();
}

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
