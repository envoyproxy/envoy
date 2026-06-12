#pragma once

#include "envoy/common/optref.h"
#include "envoy/formatter/http_formatter_context.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/dynamic_modules/abi/abi.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

using HeadersMapOptConstRef = OptRef<const Http::HeaderMap>;

/**
 * Shared read-only context accessors used by Dynamic Module extensions to expose Envoy request and
 * response state across the C ABI boundary. The access logger and formatter extensions both wrap
 * these helpers in their own callbacks, so the generic attribute, header, metadata, and local reply
 * body logic lives in a single place.
 *
 * All buffers returned point into Envoy-owned memory that is valid for the duration of the
 * originating callback. The helpers never allocate or copy.
 */
class ContextAccessor {
public:
  // Resolve the header map for the given type from the formatting context. Supported types are
  // RequestHeader, ResponseHeader, and ResponseTrailer.
  static HeadersMapOptConstRef headerMapByType(const Formatter::Context& context,
                                               envoy_dynamic_module_type_http_header_type type);

  // Fill result_headers with all entries of the resolved header map. The array must be
  // pre-allocated with at least map->size() entries.
  static bool getHeaders(HeadersMapOptConstRef map,
                         envoy_dynamic_module_type_envoy_http_header* result_headers);

  // Look up a single header value by key. index selects the value for multi-value headers and
  // total_count_out, when non-null, receives the total number of values for the key.
  static bool getHeaderValue(HeadersMapOptConstRef map, envoy_dynamic_module_type_module_buffer key,
                             envoy_dynamic_module_type_envoy_buffer* result, size_t index,
                             size_t* total_count_out);

  // Get a string attribute from the stream info. Returns false when the attribute is unavailable or
  // not a string.
  static bool getAttributeString(const StreamInfo::StreamInfo& stream_info,
                                 envoy_dynamic_module_type_attribute_id attribute_id,
                                 envoy_dynamic_module_type_envoy_buffer* result);

  // Get an integer attribute from the stream info. Returns false when the attribute is unavailable
  // or not an integer.
  static bool getAttributeInt(const StreamInfo::StreamInfo& stream_info,
                              envoy_dynamic_module_type_attribute_id attribute_id,
                              uint64_t* result);

  // Get a boolean attribute from the stream info. Returns false when the attribute is unavailable
  // or not a boolean.
  static bool getAttributeBool(const StreamInfo::StreamInfo& stream_info,
                               envoy_dynamic_module_type_attribute_id attribute_id, bool* result);

  // Get a value from dynamic metadata by filter name and dotted key path. Only string values are
  // returned, matching the zero-copy ABI contract.
  static bool getDynamicMetadata(const StreamInfo::StreamInfo& stream_info,
                                 envoy_dynamic_module_type_module_buffer filter_name,
                                 envoy_dynamic_module_type_module_buffer path,
                                 envoy_dynamic_module_type_envoy_buffer* result);

  // Get the local reply body from the formatting context. Returns false when there is no body.
  static bool getLocalReplyBody(const Formatter::Context& context,
                                envoy_dynamic_module_type_envoy_buffer* result);

  // Convert an Envoy access log type to the ABI enum. Unknown values map to NotSet so that only
  // valid enumerators ever cross the ABI boundary.
  static envoy_dynamic_module_type_access_log_type
  accessLogTypeToAbi(Formatter::AccessLogType log_type);
};

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
