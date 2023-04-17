#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/data/accesslog/v3/accesslog.pb.h"

namespace Envoy {
namespace AccessLog {

using AccessLogTypeProto = envoy::data::accesslog::v3::AccessLogCommon;

/**
 * Utility class for AccessLog.
 */
class Utility {
public:
  /**
   * @param access_log_type supplies the access log type.
   * @return a string representation of the access log type.
   */
  static const std::string& getAccessLogTypeString(const AccessLogType access_log_type);

  /**
   * @param access_log_type supplies the access log type.
   * @return a string representation of the access log type.
   */
  static AccessLogTypeProto::AccessLogType
  getAccessLogTypeProto(const AccessLogType access_log_type);
};

} // namespace AccessLog
} // namespace Envoy
