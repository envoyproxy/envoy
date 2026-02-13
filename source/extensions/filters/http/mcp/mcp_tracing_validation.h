#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

/**
 * Utilities for validating W3C tracing-related headers.
 * See: https://www.w3.org/TR/trace-context/ and https://www.w3.org/TR/baggage/
 */
class McpTracingValidation {
public:
  /**
   * Verifies that the given string is a valid traceparent header.
   */
  static bool isValidTraceParent(absl::string_view trace_parent);

  /**
   * Verifies that the given string is a valid tracestate header.
   */
  static bool isValidTraceState(absl::string_view trace_state);

  /**
   * Verifies that the given string is a valid baggage header.
   */
  static bool isValidBaggage(absl::string_view baggage);
};

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
