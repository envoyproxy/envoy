#pragma once

#include "envoy/common/optref.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

OptRef<const envoy::config::core::v3::RetryPolicy> optionalRetryPolicy(
    const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config);

}
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
