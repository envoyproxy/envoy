#pragma once

#include "source/extensions/propagators/opentelemetry/propagator.h"
#include "source/common/tracing/trace_context_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace OpenTelemetry {

/**
 * W3C Baggage propagator.
 * Handles baggage header for cross-cutting concerns according to W3C Baggage specification.
 * Note: This propagator only handles baggage, not trace context.
 * It should be used in combination with trace context propagators.
 * See: https://w3c.github.io/baggage/
 */
class BaggagePropagator : public TextMapPropagator {
public:
  BaggagePropagator();

  // TextMapPropagator
  absl::StatusOr<SpanContext> extract(const Tracing::TraceContext& trace_context) override;
  void inject(const SpanContext& span_context, Tracing::TraceContext& trace_context) override;
  std::vector<std::string> fields() const override;
  std::string name() const override;

private:
  /**
   * Parse baggage entries from a baggage header value.
   * @param baggage_header The baggage header value to parse.
   * @return Map of baggage key-value pairs, or empty map if parsing fails.
   */
  absl::flat_hash_map<std::string, std::string> parseBaggage(absl::string_view baggage_header);

  /**
   * Format baggage entries into a baggage header value.
   * @param baggage_entries The baggage key-value pairs to format.
   * @return Formatted baggage header value.
   */
  std::string formatBaggage(const absl::flat_hash_map<std::string, std::string>& baggage_entries);

  /**
   * Validate and URL-decode a baggage key or value.
   * @param input The input string to decode.
   * @return Decoded string, or empty string if invalid.
   */
  std::string urlDecode(absl::string_view input);

  /**
   * URL-encode a baggage key or value.
   * @param input The input string to encode.
   * @return Encoded string.
   */
  std::string urlEncode(absl::string_view input);

  /**
   * Validate a baggage key according to W3C specification.
   * @param key The key to validate.
   * @return True if valid, false otherwise.
   */
  bool isValidBaggageKey(absl::string_view key);

  /**
   * Validate a baggage value according to W3C specification.
   * @param value The value to validate.
   * @return True if valid, false otherwise.
   */
  bool isValidBaggageValue(absl::string_view value);

  const Tracing::TraceContextHandler baggage_header_;
};

} // namespace OpenTelemetry
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
