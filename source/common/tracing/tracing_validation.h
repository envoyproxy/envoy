#pragma once

#include <cstddef>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Tracing {

/**
 * Utilities for validating W3C tracing-related headers.
 * See: https://www.w3.org/TR/trace-context/ and https://www.w3.org/TR/baggage/
 */

// See https://www.w3.org/TR/trace-context/#traceparent-header
class W3cTraceConstants {
public:
  static constexpr size_t kVersionHexSize = 2;
  static constexpr size_t kTraceIdHexSize = 32;
  static constexpr size_t kParentIdHexSize = 16;
  static constexpr size_t kTraceFlagsHexSize = 2;

  static constexpr size_t kTraceparentHeaderSize = kVersionHexSize + 1 +  //
                                                kTraceIdHexSize + 1 +  //
                                                kParentIdHexSize + 1 + //
                                                kTraceFlagsHexSize;
  static_assert(kTraceparentHeaderSize == 55);
};

/**
 * Verifies that the given string is a valid traceparent header.
 */
bool isValidTraceParent(absl::string_view trace_parent);

/**
 * Verifies that the given string is a valid tracestate header.
 */
bool isValidTraceState(absl::string_view trace_state);

/**
 * Verifies that the given string is a valid baggage header.
 */
bool isValidBaggage(absl::string_view baggage);

} // namespace Tracing
} // namespace Envoy
