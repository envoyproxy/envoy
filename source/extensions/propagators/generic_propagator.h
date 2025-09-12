#pragma once

#include "envoy/tracing/trace_context.h"

#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/extensions/propagators/trace_context.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {

/**
 * Abstract interface for trace context propagation using generic trace context types.
 * Each propagator handles a specific format (W3C, B3, etc.) and is completely
 * independent of any specific tracer implementation.
 *
 * This interface complies with propagation specifications while being tracer-agnostic:
 * - B3 Propagation: https://github.com/openzipkin/b3-propagation
 * - W3C Trace Context: https://www.w3.org/TR/trace-context/
 * - W3C Baggage: https://www.w3.org/TR/baggage/
 *
 * Key features:
 * - Generic trace context types independent of any tracer
 * - Error handling that preserves existing valid values
 * - Support for field enumeration to facilitate carrier pre-allocation
 * - Case-insensitive header handling through Envoy's TraceContext abstraction
 */
class GenericPropagator {
public:
  virtual ~GenericPropagator() = default;

  /**
   * Extract span context from trace context headers.
   * @param trace_context The HTTP headers to extract from.
   * @return Generic SpanContext if extraction succeeds, error status otherwise.
   */
  virtual absl::StatusOr<SpanContext> extract(const Tracing::TraceContext& trace_context) = 0;

  /**
   * Inject span context into trace context headers.
   * @param span_context The generic span context to inject.
   * @param trace_context The HTTP headers to inject into.
   */
  virtual void inject(const SpanContext& span_context, Tracing::TraceContext& trace_context) = 0;

  /**
   * Extract baggage from trace context headers (if supported by propagator).
   * @param trace_context The HTTP headers to extract from.
   * @return Baggage if extraction succeeds, error status otherwise.
   */
  virtual absl::StatusOr<Baggage> extractBaggage(const Tracing::TraceContext& trace_context);

  /**
   * Inject baggage into trace context headers (if supported by propagator).
   * @param baggage The baggage to inject.
   * @param trace_context The HTTP headers to inject into.
   */
  virtual void injectBaggage(const Baggage& baggage, Tracing::TraceContext& trace_context);

  /**
   * @return The list of header names this propagator reads/writes.
   */
  virtual std::vector<std::string> fields() const = 0;

  /**
   * @return The name of this propagator (for logging/debugging).
   */
  virtual std::string name() const = 0;
};

using GenericPropagatorPtr = std::unique_ptr<GenericPropagator>;

/**
 * Manages multiple generic propagators and coordinates extraction/injection.
 *
 * Implements composite propagator functionality:
 * - Extract: Tries propagators in order, first successful extraction wins
 * - Inject: Injects using all configured propagators for maximum interoperability
 * - Preserves existing context values on extraction failure
 * - Supports both trace context and baggage propagation
 */
class GenericCompositePropagator : Logger::Loggable<Logger::Id::tracing> {
public:
  explicit GenericCompositePropagator(std::vector<GenericPropagatorPtr> propagators);

  /**
   * Try to extract span context using configured propagators in order.
   * @param trace_context The HTTP headers to extract from.
   * @return SpanContext from first successful propagator, error if none succeed.
   */
  absl::StatusOr<SpanContext> extract(const Tracing::TraceContext& trace_context);

  /**
   * Inject span context using all configured propagators.
   * @param span_context The span context to inject.
   * @param trace_context The HTTP headers to inject into.
   */
  void inject(const SpanContext& span_context, Tracing::TraceContext& trace_context);

  /**
   * Try to extract baggage using configured propagators in order.
   * @param trace_context The HTTP headers to extract from.
   * @return Baggage from first successful propagator, error if none succeed.
   */
  absl::StatusOr<Baggage> extractBaggage(const Tracing::TraceContext& trace_context);

  /**
   * Inject baggage using all configured propagators.
   * @param baggage The baggage to inject.
   * @param trace_context The HTTP headers to inject into.
   */
  void injectBaggage(const Baggage& baggage, Tracing::TraceContext& trace_context);

  /**
   * Check if any propagation headers are present.
   * @param trace_context The HTTP headers to check.
   * @return True if any propagator detects its headers.
   */
  bool propagationHeaderPresent(const Tracing::TraceContext& trace_context);

private:
  std::vector<GenericPropagatorPtr> propagators_;
};

using GenericCompositePropagatorPtr = std::unique_ptr<GenericCompositePropagator>;

} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
