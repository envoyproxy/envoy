#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"

#include "library/common/types/c_types.h"

namespace Envoy {
namespace Http {
namespace Utility {

/**
 * Transform envoy_headers to the supplied HeaderMap
 * This function copies the content.
 * @param envoy_result_headers, the Envoy headers to fill in. These headers must have a formatter
 *        set.
 * @param headers, the envoy_headers to transform. headers is free'd. Use after function return is
 * unsafe.
 */
void toEnvoyHeaders(HeaderMap& envoy_result_headers, envoy_headers headers);

/**
 * Transform envoy_headers to RequestHeaderMap.
 * This function copies the content.
 * @param headers, the envoy_headers to transform. headers is free'd. Use after function return is
 * unsafe.
 * @return RequestHeaderMapPtr, the RequestHeaderMap 1:1 transformation of the headers param.
 */
RequestHeaderMapPtr toRequestHeaders(envoy_headers headers);

/**
 * Transform envoy_headers to RequestHeaderMap.
 * This function copies the content.
 * @param trailers, the envoy_headers (trailers) to transform. headers is free'd. Use after function
 * return is unsafe.
 * @return RequestTrailerMapPtr, the RequestTrailerMap 1:1 transformation of the headers param.
 */
RequestTrailerMapPtr toRequestTrailers(envoy_headers trailers);

/**
 * Transform envoy_headers to HeaderMap.
 * This function copies the content.
 * Caller owns the allocated bytes for the return value, and needs to free after use.
 * @param headers, the HeaderMap to transform.
 * @return envoy_headers, the HeaderMap 1:1 transformation of the headers param.
 */
envoy_headers toBridgeHeaders(const HeaderMap& headers);

} // namespace Utility
} // namespace Http
} // namespace Envoy
