#pragma once

#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * @brief A resource detector that extracts attributes from the OTEL_RESOURCE_ATTRIBUTES environment
 * variable.
 * @see
 * https://github.com/open-telemetry/opentelemetry-specification/blob/v1.24.0/specification/resource/sdk.md#detecting-resource-information-from-the-environment
 *
 */
class EnvironmentResourceDetector : public ResourceDetector, Logger::Loggable<Logger::Id::tracing> {
public:
  EnvironmentResourceDetector(Server::Configuration::TracerFactoryContext& context)
      : context_(context) {}
  Resource detect() override;

private:
  Server::Configuration::TracerFactoryContext&
      context_; // TODO, is keeping a reference ok (ownership)?
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
