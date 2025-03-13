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
 * Transform from Buffer::Instance to envoy_data.
 * @param data, the Buffer::Instance to transform.
 * @param max_bytes, the maximum bytes to transform or 0 to copy all available data.
 * @return envoy_data, the bridge transformation of the Buffer::Instance param.
 */
envoy_data toBridgeData(Buffer::Instance& data, uint32_t max_bytes = 0);

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

} // namespace Utility
} // namespace Bridge
} // namespace Envoy
