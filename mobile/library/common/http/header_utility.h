#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"

#include "library/common/include/c_types.h"

namespace Envoy {
namespace Http {
namespace Utility {

/**
 * Copy envoy_data into an std::string.
 * @param s the envoy_data to copy.
 * @return std::string the string constructed from s.
 */
std::string convertToString(envoy_data s);

/**
 * Transform envoy_headers to HeaderMap.
 * This function copies the content.
 * @param headers, the envoy_headers to transform.
 * @return HeaderMapPtr, the HeaderMap 1:1 transformation of the headers param.
 */
HeaderMapPtr transformHeaders(envoy_headers headers);

/**
 * Transform envoy_headers to HeaderMap.
 * This function copies the content.
 * Caller owns the allocated bytes for the return value, and needs to free after use.
 * @param headers, the HeaderMap to transform.
 * @return envoy_headers, the HeaderMap 1:1 transformation of the headers param.
 */
envoy_headers transformHeaders(const HeaderMap& headers);

} // namespace Utility
} // namespace Http
} // namespace Envoy
