#pragma once

#include <string>

#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/dynatrace_resource_detector.pb.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * Config registration for the Dynatrace resource detector. @see ResourceDetectorFactory.
 */
class DynatraceResourceDetectorFactory : public ResourceDetectorFactory {
public:
  /**
   * @brief Creates a Resource Detector that reads from the Dynatrace enrichment files.
   *
   * @see
   * https://docs.dynatrace.com/docs/shortlink/enrichment-files#oneagent-virtual-files
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
                                DynatraceResourceDetectorConfig>();
  }

  std::string name() const override {
    return "envoy.tracers.opentelemetry.resource_detectors.dynatrace";
  }
};

DECLARE_FACTORY(DynatraceResourceDetectorFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
