#include "source/common/access_log/utility.h"

#include <string>

#include "envoy/access_log/access_log.h"

#include "source/common/common/empty_string.h"

namespace Envoy {
namespace AccessLog {

const std::string& Utility::getAccessLogTypeString(const AccessLogType access_log_type) {
  switch (access_log_type) {
  case AccessLogType::TcpUpstreamConnected:
    return AccessLogTypeStrings::get().TcpUpstreamConnected;
  case AccessLogType::TcpPeriodic:
    return AccessLogTypeStrings::get().TcpPeriodic;
  case AccessLogType::TcpEnd:
    return AccessLogTypeStrings::get().TcpEnd;
  case AccessLogType::HcmNewRequest:
    return AccessLogTypeStrings::get().HcmNewRequest;
  case AccessLogType::HcmPeriodic:
    return AccessLogTypeStrings::get().HcmPeriodic;
  case AccessLogType::HcmEnd:
    return AccessLogTypeStrings::get().HcmEnd;
  case AccessLogType::RouterNewRequest:
    return AccessLogTypeStrings::get().RouterNewRequest;
  case AccessLogType::RouterPeriodic:
    return AccessLogTypeStrings::get().RouterPeriodic;
  case AccessLogType::RouterEnd:
    return AccessLogTypeStrings::get().RouterEnd;
  case AccessLogType::NotSet:
    break;
  }

  return EMPTY_STRING;
}

} // namespace AccessLog
} // namespace Envoy
