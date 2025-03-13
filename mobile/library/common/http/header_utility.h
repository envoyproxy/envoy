#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "library/common/types/c_types.h"

namespace Envoy {
namespace Http {
namespace Utility {

/*
 * Returns the proper status code for onLocalReply
 * @param reply the local reply data for the stream.
 * @param stream_info the info for the stream.
 */
Http::LocalErrorStatus statusForOnLocalReply(const StreamDecoderFilter::LocalReplyData& reply,
                                             const StreamInfo::StreamInfo& stream_info);

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

/** Creates an empty `RequestHeaderMapPtr` with a preserve case header formatter. */
RequestHeaderMapPtr createRequestHeaderMapPtr();

/** Creates an empty `RequestTrailerMapPtr`. */
RequestTrailerMapPtr createRequestTrailerMapPtr();

/**
 * Transform envoy_headers to HeaderMap.
 * This function copies the content.
 * Caller owns the allocated bytes for the return value, and needs to free after use.
 * @param headers, the HeaderMap to transform.
 * @param alpn, the optional alpn to add to the headers.
 * @return envoy_headers, the HeaderMap 1:1 transformation of the headers param.
 */
envoy_headers toBridgeHeaders(const HeaderMap& headers, absl::string_view alpn = "");

} // namespace Utility
} // namespace Http
} // namespace Envoy
