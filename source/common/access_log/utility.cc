#include "source/common/access_log/utility.h"

#include <string>

#include "envoy/access_log/access_log.h"

namespace Envoy {
namespace AccessLog {

const std::string& Utility::getAccessLogTypeString(const AccessLogTypeEnum access_log_type) {
  switch (access_log_type) {
  case AccessLogType::TcpUpstreamConnected:
    return AccessLogTypeStrings::get().TcpUpstreamConnected;
  case AccessLogType::TcpPeriodic:
    return AccessLogTypeStrings::get().TcpPeriodic;
  case AccessLogType::TcpEnd:
    return AccessLogTypeStrings::get().TcpEnd;
  case AccessLogType::DownstreamStart:
    return AccessLogTypeStrings::get().DownstreamStart;
  case AccessLogType::DownstreamPeriodic:
    return AccessLogTypeStrings::get().DownstreamPeriodic;
  case AccessLogType::DownstreamEnd:
    return AccessLogTypeStrings::get().DownstreamEnd;
  case AccessLogType::UpstreamStart:
    return AccessLogTypeStrings::get().UpstreamStart;
  case AccessLogType::UpstreamPeriodic:
    return AccessLogTypeStrings::get().UpstreamPeriodic;
  case AccessLogType::UpstreamEnd:
    return AccessLogTypeStrings::get().UpstreamEnd;
  default:
    return AccessLogTypeStrings::get().NotSet;
  }
}

} // namespace AccessLog
} // namespace Envoy
