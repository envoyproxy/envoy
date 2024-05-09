#include "library/common/bridge/utility.h"

#include <string>

namespace Envoy {
namespace Bridge {
namespace Utility {

envoy_error_code_t errorCodeFromLocalStatus(Http::Code status) {
  switch (status) {
  case Http::Code::RequestTimeout:
    return ENVOY_REQUEST_TIMEOUT;
  case Http::Code::PayloadTooLarge:
    return ENVOY_BUFFER_LIMIT_EXCEEDED;
  case Http::Code::ServiceUnavailable:
    return ENVOY_CONNECTION_FAILURE;
  default:
    return ENVOY_UNDEFINED_ERROR;
  }
}

envoy_map makeEnvoyMap(std::initializer_list<std::pair<std::string, std::string>> map) {
  return makeEnvoyMap<std::initializer_list<std::pair<std::string, std::string>>>(map);
}

envoy_error toBridgeError(const EnvoyError& error) {
  envoy_error error_bridge{};
  error_bridge.message = Data::Utility::copyToBridgeData(error.message);
  error_bridge.error_code = error.error_code;
  if (error.attempt_count.has_value()) {
    error_bridge.attempt_count = *error.attempt_count;
  }
  return error_bridge;
}

} // namespace Utility
} // namespace Bridge
} // namespace Envoy
