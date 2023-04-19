#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"

namespace Envoy {
namespace AccessLog {

/**
 * Utility class for AccessLog.
 */
class Utility {
public:
  /**
   * @param access_log_type supplies the access log type.
   * @return a string representation of the access log type.
   */
  static const std::string& getAccessLogTypeString(const AccessLogTypeEnum access_log_type);
};

} // namespace AccessLog
} // namespace Envoy
