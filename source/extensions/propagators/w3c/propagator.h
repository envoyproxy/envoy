#pragma once

#include "envoy/tracing/trace_context.h"

#include "source/common/singleton/const_singleton.h"
#include "source/common/tracing/trace_context_impl.h"
#include "source/extensions/propagators/w3c/trace_context.h"

#include "absl/status/statusor.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace W3C {

/**
 * W3C Trace Context constants for header handling.
 */
class W3CConstantValues {
public:
  const Tracing::TraceContextHandler TRACE_PARENT{std::string(Constants::kTraceparentHeader)};
  const Tracing::TraceContextHandler TRACE_STATE{std::string(Constants::kTracestateHeader)};
  const Tracing::TraceContextHandler BAGGAGE{std::string(Constants::kBaggageHeader)};
};

using W3CConstants = ConstSingleton<W3CConstantValues>;

/**
 * W3C Trace Context and Baggage Propagator.
 * Provides complete specification-compliant extraction and injection of W3C distributed tracing
 * headers.
 *
 * This component implements the complete W3C specifications:
 * - W3C Trace Context: https://www.w3.org/TR/trace-context/
 * - W3C Baggage: https://www.w3.org/TR/baggage/
 *
 * SPECIFICATION COMPLIANCE:
 * W3C Trace Context:
 *    - Traceparent format validation (version-traceId-parentId-traceFlags)
 *    - Header case-insensitivity per HTTP specification
 *    - Future version compatibility (versions > 00)
 *    - Zero trace ID and span ID rejection
 *    - Tracestate concatenation with comma separator
 *    - Proper hex string validation and length enforcement
 *
 * W3C Baggage:
 *    - URL encoding/decoding for keys, values, and properties
 *    - 8KB size limit enforcement with graceful handling
 *    - Baggage member properties support (semicolon-separated)
 *    - Malformed header tolerance while preserving valid data
 *    - Comma-separated member parsing with format validation
 *
 * This provides a reusable interface for different tracing systems while
 * ensuring complete W3C specification compliance and backward compatibility.
 */
class Propagator {
public:
  /**
   * Check if W3C trace context headers are present.
   * @param trace_context the trace context to check
   * @return true if traceparent header is present
   */
  static bool isPresent(const Tracing::TraceContext& trace_context);

  /**
   * Extract W3C trace context from headers.
   * @param trace_context the trace context containing headers
   * @return W3C TraceContext or error status if extraction fails
   */
  static absl::StatusOr<TraceContext> extract(const Tracing::TraceContext& trace_context);

  /**
   * Inject W3C trace context into headers.
   * @param w3c_context the W3C trace context to inject
   * @param trace_context the trace context to inject headers into
   */
  static void inject(const TraceContext& w3c_context, Tracing::TraceContext& trace_context);

  /**
   * Create a new trace context with a new span.
   * @param parent_context the parent W3C trace context
   * @param new_span_id the new span ID (32 hex characters)
   * @return new W3C TraceContext with updated parent-id
   */
  static absl::StatusOr<TraceContext> createChild(const TraceContext& parent_context,
                                                  absl::string_view new_span_id);

  /**
   * Create a new root trace context.
   * @param trace_id the trace ID (32 hex characters)
   * @param span_id the span ID (16 hex characters)
   * @param sampled whether the trace is sampled
   * @return new W3C TraceContext
   */
  static absl::StatusOr<TraceContext> createRoot(absl::string_view trace_id,
                                                 absl::string_view span_id, bool sampled);

  /**
   * Check if W3C baggage header is present.
   * @param trace_context the trace context to check
   * @return true if baggage header is present
   */
  static bool isBaggagePresent(const Tracing::TraceContext& trace_context);

  /**
   * Extract W3C baggage from headers.
   * @param trace_context the trace context containing headers
   * @return W3C Baggage or error status if extraction fails
   */
  static absl::StatusOr<Baggage> extractBaggage(const Tracing::TraceContext& trace_context);

  /**
   * Inject W3C baggage into headers.
   * @param baggage the W3C baggage to inject
   * @param trace_context the trace context to inject headers into
   */
  static void injectBaggage(const Baggage& baggage, Tracing::TraceContext& trace_context);

  // Helper to validate hex string of specific length
  static bool isValidHexString(absl::string_view input, size_t expected_length);
};

/**
 * Utility class for working with W3C trace context in existing Envoy tracers.
 * Provides backward compatibility and easy integration.
 */
class TracingHelper {
public:
  /**
   * Extract trace parent information in a format compatible with existing tracers.
   * @param trace_context the trace context containing headers
   * @return struct containing extracted values or nullopt if not present/invalid
   */
  struct ExtractedContext {
    std::string version;
    std::string trace_id;
    std::string span_id;
    std::string trace_flags;
    bool sampled;
    std::string tracestate;
  };

  static absl::optional<ExtractedContext>
  extractForTracer(const Tracing::TraceContext& trace_context);

  /**
   * Check if traceparent header is present (for backward compatibility).
   */
  static bool traceparentPresent(const Tracing::TraceContext& trace_context);
};

/**
 * Utility class for working with W3C baggage in existing Envoy tracers.
 * Provides integration with the standard Span baggage interface.
 */
class BaggageHelper {
public:
  /**
   * Extract baggage value by key for tracer getBaggage() implementation.
   * @param trace_context the trace context containing headers
   * @param key the baggage key to look up
   * @return the baggage value if found, empty string otherwise
   */
  static std::string getBaggageValue(const Tracing::TraceContext& trace_context,
                                     absl::string_view key);

  /**
   * Set baggage value for tracer setBaggage() implementation.
   * @param trace_context the trace context to modify
   * @param key the baggage key
   * @param value the baggage value
   * @return true if successfully set, false if size limits exceeded
   */
  static bool setBaggageValue(Tracing::TraceContext& trace_context, absl::string_view key,
                              absl::string_view value);

  /**
   * Get all baggage as a map for tracer integration.
   * @param trace_context the trace context containing headers
   * @return map of all baggage key-value pairs
   */
  static std::map<std::string, std::string>
  getAllBaggage(const Tracing::TraceContext& trace_context);

  /**
   * Check if any baggage is present.
   * @param trace_context the trace context to check
   * @return true if baggage header is present and valid
   */
  static bool hasBaggage(const Tracing::TraceContext& trace_context);
};

} // namespace W3C
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
