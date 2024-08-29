#pragma once

#include <string>

#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/static_config_resource_detector.pb.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * Config registration for the static config resource detector. @see ResourceDetectorFactory.
 */
class StaticConfigResourceDetectorFactory : public ResourceDetectorFactory {
public:
  /**
   * @brief Create a Resource Detector that use static config for resource attributes.
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
                                StaticConfigResourceDetectorConfig>();
  }

  std::string name() const override {
    return "envoy.tracers.opentelemetry.resource_detectors.static_config";
  }
};

DECLARE_FACTORY(StaticConfigResourceDetectorFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
