#include "source/extensions/tracers/opentelemetry/resource_detectors/per_route/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

static Registry::RegisterFactory<PerRouteResourceDetectorFactory, ResourceDetectorFactory>
    register_;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
