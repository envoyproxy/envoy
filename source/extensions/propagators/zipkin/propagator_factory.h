#pragma once

#include "envoy/api/api.h"

#include "source/common/common/logger.h"
#include "source/extensions/propagators/zipkin/propagator.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace Zipkin {

/**
 * Factory for creating propagators for Zipkin tracer.
 * This provides Zipkin-specific propagator configuration using only Zipkin types.
 */
class PropagatorFactory : public Logger::Loggable<Logger::Id::tracing> {
public:
  /**
   * Create a composite propagator from a list of propagator names.
   * @param propagator_names List of propagator names (e.g., "b3", "tracecontext").
   * @return CompositePropagator containing the specified propagators.
   */
  static CompositePropagatorPtr createPropagators(const std::vector<std::string>& propagator_names);

  /**
   * Get the default propagator configuration for Zipkin (B3 format).
   * @return CompositePropagator with Zipkin's default configuration.
   */
  static CompositePropagatorPtr createDefaultPropagators();

private:
  static TextMapPropagatorPtr createPropagator(const std::string& name);
};

} // namespace Zipkin
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
