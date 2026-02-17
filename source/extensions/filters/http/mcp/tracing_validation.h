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
namespace TracingValidation {

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

} // namespace TracingValidation
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
