#include "source/extensions/tracers/opentelemetry/resource_detectors/static/config.h"

#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/static_config_resource_detector.pb.h"
#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/static_config_resource_detector.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/static/static_config_resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

ResourceDetectorPtr StaticConfigResourceDetectorFactory::createResourceDetector(
    const Protobuf::Message& message, Server::Configuration::TracerFactoryContext& context) {

  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      dynamic_cast<const ProtobufWkt::Any&>(message), context.messageValidationVisitor(), *this);

  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
          StaticConfigResourceDetectorConfig&>(*mptr, context.messageValidationVisitor());

  return std::make_unique<StaticConfigResourceDetector>(proto_config, context);
}

/**
 * Static registration for the static config resource detector factory. @see RegisterFactory.
 */
REGISTER_FACTORY(StaticConfigResourceDetectorFactory, ResourceDetectorFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
