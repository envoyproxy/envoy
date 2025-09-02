#pragma once

#include "envoy/tracing/trace_context.h"

#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/extensions/tracers/zipkin/span_context.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace Zipkin {

/**
 * Abstract interface for Zipkin trace context propagation.
 * Each propagator handles a specific format (B3, W3C, etc.) using Zipkin-specific types.
 *
 * This interface is designed specifically for Zipkin tracer compatibility while
 * supporting standard propagation formats:
 * - B3 Propagation: https://github.com/openzipkin/b3-propagation
 * - W3C Trace Context: https://www.w3.org/TR/trace-context/
 *
 * Key features:
 * - Uses Zipkin-specific SpanContext type for maximum compatibility
 * - Error handling that preserves existing valid values
 * - Support for field enumeration to facilitate carrier pre-allocation
 * - Case-insensitive header handling through Envoy's TraceContext abstraction
 */
class TextMapPropagator {
public:
  virtual ~TextMapPropagator() = default;

  /**
   * Extract span context from trace context headers.
   * @param trace_context The HTTP headers to extract from.
   * @return Zipkin SpanContext if extraction succeeds, error status otherwise.
   */
  virtual absl::StatusOr<Extensions::Tracers::Zipkin::SpanContext>
  extract(const Tracing::TraceContext& trace_context) = 0;

  /**
   * Inject span context into trace context headers.
   * @param span_context The Zipkin span context to inject.
   * @param trace_context The HTTP headers to inject into.
   */
  virtual void inject(const Extensions::Tracers::Zipkin::SpanContext& span_context,
                      Tracing::TraceContext& trace_context) = 0;

  /**
   * @return The list of header names this propagator reads/writes.
   */
  virtual std::vector<std::string> fields() const = 0;

  /**
   * @return The name of this propagator (for logging/debugging).
   */
  virtual std::string name() const = 0;
};

using TextMapPropagatorPtr = std::unique_ptr<TextMapPropagator>;

/**
 * Manages multiple Zipkin propagators and coordinates extraction/injection.
 *
 * Implements composite propagator functionality for Zipkin:
 * - Extract: Tries propagators in order, first successful extraction wins
 * - Inject: Injects using all configured propagators for maximum interoperability
 * - Preserves existing context values on extraction failure
 * - Uses Zipkin-specific types throughout
 */
class CompositePropagator : Logger::Loggable<Logger::Id::tracing> {
public:
  explicit CompositePropagator(std::vector<TextMapPropagatorPtr> propagators);

  /**
   * Try to extract span context using configured propagators in order.
   * @param trace_context The HTTP headers to extract from.
   * @return Zipkin SpanContext from first successful propagator, error if none succeed.
   */
  absl::StatusOr<Extensions::Tracers::Zipkin::SpanContext>
  extract(const Tracing::TraceContext& trace_context);

  /**
   * Inject span context using all configured propagators.
   * @param span_context The Zipkin span context to inject.
   * @param trace_context The HTTP headers to inject into.
   */
  void inject(const Extensions::Tracers::Zipkin::SpanContext& span_context,
              Tracing::TraceContext& trace_context);

  /**
   * Check if any propagation headers are present.
   * @param trace_context The HTTP headers to check.
   * @return True if any propagator detects its headers.
   */
  bool propagationHeaderPresent(const Tracing::TraceContext& trace_context);

private:
  std::vector<TextMapPropagatorPtr> propagators_;
};

using CompositePropagatorPtr = std::unique_ptr<CompositePropagator>;

} // namespace Zipkin
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
