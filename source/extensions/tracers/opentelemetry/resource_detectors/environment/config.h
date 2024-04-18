#pragma once

#include <string>

#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/environment_resource_detector.pb.h"

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
   * @param message The resource detector configuration.
   * @param context The tracer factory context.
   * @return ResourceDetectorPtr
   */
  ResourceDetectorPtr
  createResourceDetector(const Protobuf::Message& message,
                         Server::Configuration::TracerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
                                EnvironmentResourceDetectorConfig>();
  }

  std::string name() const override {
    return "envoy.tracers.opentelemetry.resource_detectors.environment";
  }
};

DECLARE_FACTORY(EnvironmentResourceDetectorFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
