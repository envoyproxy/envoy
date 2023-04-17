#include "source/common/access_log/utility.h"

#include <string>

#include "envoy/access_log/access_log.h"

#include "source/common/common/empty_string.h"

namespace Envoy {
namespace AccessLog {

const std::string& Utility::getAccessLogTypeString(const AccessLogType access_log_type) {
  switch (access_log_type) {
  case AccessLogType::NotSet:
    return AccessLogTypeStrings::get().NotSet;
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

AccessLogTypeProto::AccessLogType
Utility::getAccessLogTypeProto(const AccessLogType access_log_type) {
  switch (access_log_type) {
  case AccessLogType::NotSet:
    return AccessLogTypeProto::NotSet;
  case AccessLogType::TcpUpstreamConnected:
    return AccessLogTypeProto::TcpUpstreamConnected;
  case AccessLogType::TcpPeriodic:
    return AccessLogTypeProto::TcpPeriodic;
  case AccessLogType::TcpEnd:
    return AccessLogTypeProto::TcpEnd;
  case AccessLogType::DownstreamStart:
    return AccessLogTypeProto::DownstreamStart;
  case AccessLogType::DownstreamPeriodic:
    return AccessLogTypeProto::DownstreamPeriodic;
  case AccessLogType::DownstreamEnd:
    return AccessLogTypeProto::DownstreamEnd;
  case AccessLogType::UpstreamStart:
    return AccessLogTypeProto::UpstreamStart;
  case AccessLogType::UpstreamPeriodic:
    return AccessLogTypeProto::UpstreamPeriodic;
  case AccessLogType::UpstreamEnd:
    return AccessLogTypeProto::UpstreamEnd;
  default:
    return AccessLogTypeProto::NotSet;
  }
}

} // namespace AccessLog
} // namespace Envoy
