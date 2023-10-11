#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

class Utility {
public:
  static void extractCommonAccessLogProperties(
      envoy::data::accesslog::v3::AccessLogCommon& common_access_log,
      const Http::RequestHeaderMap& request_header, const StreamInfo::StreamInfo& stream_info,
      const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig&
          filter_states_to_log,
      AccessLog::AccessLogType access_log_type);

  static void responseFlagsToAccessLogResponseFlags(
      envoy::data::accesslog::v3::AccessLogCommon& common_access_log,
      const StreamInfo::StreamInfo& stream_info);
};

bool extractFilterStateData(const StreamInfo::FilterState& filter_state, const std::string& key,
                            envoy::data::accesslog::v3::AccessLogCommon& common_access_log);

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
