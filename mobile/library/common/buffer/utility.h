#pragma once

#include "envoy/buffer/buffer.h"

#include "library/common/types/c_types.h"

namespace Envoy {
namespace Buffer {
namespace Utility {

/**
 * Transform envoy_data to Envoy::Buffer::Instance.
 * @param headers, the envoy_data to transform.
 * @return Envoy::Buffer::InstancePtr, the native transformation of the envoy_data param.
 */
Buffer::InstancePtr toInternalData(envoy_data data);

/**
 * Transform from Buffer::Instance to envoy_data.
 * @param data, the Buffer::Instance to transform.
 * @return envoy_data, the bridge transformation of the Buffer::Instance param.
 */
envoy_data toBridgeData(Buffer::Instance&);

/**
 * Copy from string to envoy_data.
 * @param str, the string to copy.
 * @return envoy_data, the copy produced of the original string.
 */
envoy_data copyToBridgeData(absl::string_view);

/**
 * Copy from Buffer::Instance to envoy_data.
 * @param data, the Buffer::Instance to copy.
 * @return envoy_data, the copy produced from the Buffer::Instance param.
 */
envoy_data copyToBridgeData(const Buffer::Instance&);

} // namespace Utility
} // namespace Buffer
} // namespace Envoy
