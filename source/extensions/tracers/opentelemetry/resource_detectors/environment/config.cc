#include "source/extensions/tracers/opentelemetry/resource_detectors/environment/config.h"

#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/environment_resource_detector.pb.h"
#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/environment_resource_detector.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/environment/environment_resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

ResourceDetectorPtr EnvironmentResourceDetectorFactory::createResourceDetector(
    const Protobuf::Message& message, Server::Configuration::TracerFactoryContext& context) {

  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      dynamic_cast<const ProtobufWkt::Any&>(message), context.messageValidationVisitor(), *this);

  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
          EnvironmentResourceDetectorConfig&>(*mptr, context.messageValidationVisitor());

  return std::make_unique<EnvironmentResourceDetector>(proto_config, context);
}

/**
 * Static registration for the Env resource detector factory. @see RegisterFactory.
 */
REGISTER_FACTORY(EnvironmentResourceDetectorFactory, ResourceDetectorFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
