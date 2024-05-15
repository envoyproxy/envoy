#pragma once

#include <string>

#include "envoy/buffer/buffer.h"

#include "source/common/http/codes.h"

#include "library/common/engine_types.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Bridge {
namespace Utility {

// Converts from `EnvoyError` to `envoy_error`.
envoy_error toBridgeError(const EnvoyError& envoy_error);

/**
 * Transform envoy_data to Envoy::Buffer::Instance.
 * @param headers, the envoy_data to transform.
 * @return Envoy::Buffer::InstancePtr, the native transformation of the envoy_data param.
 */
Buffer::InstancePtr toInternalData(envoy_data data);

/**
 * Transform from Buffer::Instance to envoy_data.
 * @param data, the Buffer::Instance to transform.
 * @param max_bytes, the maximum bytes to transform or 0 to copy all available data.
 * @return envoy_data, the bridge transformation of the Buffer::Instance param.
 */
envoy_data toBridgeData(Buffer::Instance& data, uint32_t max_bytes = 0);

/**
 * Transform from Buffer::Instance to envoy_data. The `Buffer::Instance` is not drained after
 * converting it to `envoy_data`.
 *
 * @param data, the Buffer::Instance to transform.
 * @param max_bytes, the maximum bytes to transform or 0 to copy all available data.
 * @return envoy_data, the bridge transformation of the Buffer::Instance param.
 */
envoy_data toBridgeDataNoDrain(const Buffer::Instance& data, uint32_t max_bytes = 0);

/**
 * Copy from string to envoy_data.
 * @param str, the string to copy.
 * @return envoy_data, the copy produced of the original string.
 */
envoy_data copyToBridgeData(absl::string_view);

/**
 * Copy from Buffer::Instance to envoy_data.
 * @param data, the Buffer::Instance to copy.
 * @param max_bytes, the maximum bytes to copy or 0 to copy all available data.
 * @return envoy_data, the copy produced from the Buffer::Instance param.
 */
envoy_data copyToBridgeData(const Buffer::Instance& data, uint32_t max_bytes = 0);

/**
 * Copy envoy_data into an std::string.
 * @param data, the envoy_data to copy.
 * @return std::string the string constructed from data.
 */
std::string copyToString(envoy_data data);

envoy_error_code_t errorCodeFromLocalStatus(Http::Code status);

// Function overload that helps resolve makeEnvoyMap({{"key", "value"}}).
envoy_map makeEnvoyMap(std::initializer_list<std::pair<std::string, std::string>> map);

// Helper that converts a C++ map-like (i.e. anything that iterates over a pair of strings) type
// into an envoy_map, copying all the values.
template <class T> envoy_map makeEnvoyMap(const T& map) {
  envoy_map new_map;
  new_map.length = std::size(map);
  new_map.entries =
      static_cast<envoy_map_entry*>(safe_malloc(sizeof(envoy_map_entry) * std::size(map)));

  uint64_t i = 0;
  for (const auto& e : map) {
    new_map.entries[i].key = copyToBridgeData(e.first);
    new_map.entries[i].value = copyToBridgeData(e.second);
    i++;
  }

  return new_map;
}

} // namespace Utility
} // namespace Bridge
} // namespace Envoy
