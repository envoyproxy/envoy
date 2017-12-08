#include "common/router/header_formatter.h"

#include <string>

#include "common/access_log/access_log_formatter.h"

#include "fmt/format.h"

namespace Envoy {
namespace Router {

RequestInfoHeaderFormatter::RequestInfoHeaderFormatter(const std::string& field_name, bool append)
    : append_(append) {
  if (field_name == "PROTOCOL") {
    field_extractor_ = [](const Envoy::AccessLog::RequestInfo& request_info) {
      return Envoy::AccessLog::AccessLogFormatUtils::protocolToString(request_info.protocol());
    };
  } else if (field_name == "CLIENT_IP") {
    field_extractor_ = [](const Envoy::AccessLog::RequestInfo& request_info) {
      return request_info.getDownstreamAddress();
    };
  } else {
    throw EnvoyException(fmt::format("field '{}' not supported as custom header", field_name));
  }
}

const std::string
RequestInfoHeaderFormatter::format(const Envoy::AccessLog::RequestInfo& request_info) const {
  return field_extractor_(request_info);
}

} // namespace Router
} // namespace Envoy
