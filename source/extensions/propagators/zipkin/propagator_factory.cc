#include "source/extensions/propagators/zipkin/propagator_factory.h"

#include "source/common/common/logger.h"
#include "source/extensions/propagators/zipkin/b3/propagator.h"
#include "source/extensions/propagators/zipkin/w3c/trace_context_propagator.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace Zipkin {

CompositePropagatorPtr
PropagatorFactory::createPropagators(const std::vector<std::string>& propagator_names) {
  std::vector<TextMapPropagatorPtr> propagators;

  for (const auto& name : propagator_names) {
    auto propagator = createPropagator(name);
    if (propagator) {
      propagators.push_back(std::move(propagator));
    } else {
      ENVOY_LOG(warn, "Unknown propagator name: {}. Ignoring.", name);
    }
  }

  if (propagators.empty()) {
    ENVOY_LOG(info, "No valid propagators specified, using default B3 format for Zipkin");
    return createDefaultPropagators();
  }

  return std::make_unique<CompositePropagator>(std::move(propagators));
}

CompositePropagatorPtr PropagatorFactory::createDefaultPropagators() {
  std::vector<TextMapPropagatorPtr> propagators;
  // Zipkin defaults to B3 format
  propagators.push_back(std::make_unique<B3Propagator>());
  return std::make_unique<CompositePropagator>(std::move(propagators));
}

TextMapPropagatorPtr PropagatorFactory::createPropagator(const std::string& name) {
  if (name == "b3") {
    return std::make_unique<B3Propagator>();
  } else if (name == "tracecontext") {
    return std::make_unique<W3CTraceContextPropagator>();
  }

  return nullptr;
}

} // namespace Zipkin
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
