#pragma once

#include "source/extensions/propagators/generic_propagator.h"
#include "source/common/tracing/trace_context_impl.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace W3C {

/**
 * W3C Baggage propagator that is tracer-agnostic.
 * This implementation is fully compliant with the W3C Baggage specification.
 *
 * Handles W3C Baggage specification features:
 * - baggage header: key1=value1,key2=value2;metadata
 * - Proper key-value pair parsing with metadata support
 * - URL encoding/decoding for keys and values
 * - Member ordering preservation
 *
 * See: https://www.w3.org/TR/baggage/
 */
class BaggagePropagator : public GenericPropagator {
public:
  BaggagePropagator();

  // GenericPropagator interface
  absl::StatusOr<SpanContext> extract(const Tracing::TraceContext& trace_context) override;
  void inject(const SpanContext& span_context, Tracing::TraceContext& trace_context) override;
  absl::StatusOr<Baggage> extractBaggage(const Tracing::TraceContext& trace_context) override;
  void injectBaggage(const Baggage& baggage, Tracing::TraceContext& trace_context) override;
  std::vector<std::string> fields() const override;
  std::string name() const override;

private:
  const Tracing::TraceContextHandler baggage_header_;

  absl::StatusOr<Baggage> parseBaggage(const std::string& baggage_value);
  std::string formatBaggage(const Baggage& baggage);
};

} // namespace W3C
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
