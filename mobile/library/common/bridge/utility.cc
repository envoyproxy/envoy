#include "library/common/bridge/utility.h"

#include <string>

#include "library/common/data/utility.h"

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

envoy_map makeEnvoyMap(std::vector<std::pair<std::string, std::string>> pairs) {
  envoy_map_entry* fields =
      static_cast<envoy_map_entry*>(safe_malloc(sizeof(envoy_map_entry) * pairs.size()));
  envoy_map new_event;
  new_event.length = 0;
  new_event.entries = fields;

  for (const auto& pair : pairs) {
    envoy_data key = Data::Utility::copyToBridgeData(pair.first);
    envoy_data value = Data::Utility::copyToBridgeData(pair.second);

    new_event.entries[new_event.length] = {key, value};
    new_event.length++;
  }

  return new_event;
}

} // namespace Utility
} // namespace Bridge
} // namespace Envoy
