#pragma once

#include "envoy/common/exception.h"
#include "envoy/tracing/tracer.h"

#include "source/common/http/header_map_impl.h"
#include "source/extensions/propagators/zipkin/propagator.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

class SpanContext;

struct ExtractorException : public EnvoyException {
  ExtractorException(const std::string& what) : EnvoyException(what) {}
};

/**
 * This class is used to SpanContext extracted from the Http header.
 * Supports B3 propagation format natively and W3C Trace Context as fallback
 * when w3c_fallback_enabled is true.
 */
class SpanContextExtractor {
public:
  /**
   * Constructor for B3-only extraction.
   * @param trace_context HTTP headers to extract from
   * @param w3c_fallback_enabled Whether to enable W3C Trace Context fallback
   */
  SpanContextExtractor(Tracing::TraceContext& trace_context, bool w3c_fallback_enabled = false);

  /**
   * Constructor with configured propagator names for specification compliance.
   * This allows respecting custom propagator configuration.
   *
   * @param trace_context HTTP headers to extract from
   * @param w3c_fallback_enabled Whether to enable W3C Trace Context fallback
   * @param propagator_names List of propagator names to use (e.g., "tracecontext", "b3")
   */
  SpanContextExtractor(Tracing::TraceContext& trace_context, bool w3c_fallback_enabled,
                       const std::vector<std::string>& propagator_names);
  ~SpanContextExtractor();
  absl::optional<bool> extractSampled();
  std::pair<SpanContext, bool> extractSpanContext(bool is_sampled);

private:
  const Tracing::TraceContext& trace_context_;
  bool w3c_fallback_enabled_;
  std::vector<std::string> propagator_names_;
  Extensions::Propagators::Zipkin::CompositePropagatorPtr composite_propagator_;
};

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
