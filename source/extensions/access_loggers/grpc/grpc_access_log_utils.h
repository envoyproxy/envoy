#pragma once

#include "envoy/data/accesslog/v3alpha/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3alpha/als.pb.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

class Utility {
public:
  static void extractCommonAccessLogProperties(
      envoy::data::accesslog::v3alpha::AccessLogCommon& common_access_log,
      const StreamInfo::StreamInfo& stream_info,
      const envoy::extensions::access_loggers::grpc::v3alpha::CommonGrpcAccessLogConfig&
          filter_states_to_log);

  static void responseFlagsToAccessLogResponseFlags(
      envoy::data::accesslog::v3alpha::AccessLogCommon& common_access_log,
      const StreamInfo::StreamInfo& stream_info);
};

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
