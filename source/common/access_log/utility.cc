#include "source/common/access_log/utility.h"

#include <string>

#include "envoy/access_log/access_log.h"

#include "source/common/common/empty_string.h"

namespace Envoy {
namespace AccessLog {

const std::string& Utility::getAccessLogTypeString(const AccessLogType access_log_type) {
  switch (access_log_type) {
  case AccessLogType::Type1:
    return AccessLogTypeStrings::get().Type1;
  }

  return EMPTY_STRING;
}

} // namespace AccessLog
} // namespace Envoy
