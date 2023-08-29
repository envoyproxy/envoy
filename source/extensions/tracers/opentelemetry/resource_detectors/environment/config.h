#pragma once

#include <string>

#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * Config registration for the Environment resource detector. @see ResourceDetectorFactory.
 */
class EnvironmentResourceDetectorFactory : public ResourceDetectorFactory {
public:
  /**
   * @brief Create a Resource Detector that reads from the OTEL_RESOURCE_ATTRIBUTES
   * environment variable.
   *
   * @param context
   * @return ResourceDetectorPtr
   */
  ResourceDetectorPtr
  createResourceDetector(Server::Configuration::TracerFactoryContext& context) override;

  std::string name() const override {
    return "envoy.tracers.opentelemetry.resource_detectors.environment";
  }
};

DECLARE_FACTORY(EnvironmentResourceDetectorFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
