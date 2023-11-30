#include "source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/config.h"

#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/dynatrace_resource_detector.pb.h"
#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/dynatrace_resource_detector.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/dynatrace_metadata_file_reader.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/dynatrace_resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

ResourceDetectorPtr DynatraceResourceDetectorFactory::createResourceDetector(
    const Protobuf::Message& message, Server::Configuration::TracerFactoryContext& context) {

  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      dynamic_cast<const ProtobufWkt::Any&>(message), context.messageValidationVisitor(), *this);

  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
          DynatraceResourceDetectorConfig&>(*mptr, context.messageValidationVisitor());

  DynatraceMetadataFileReaderPtr reader = std::make_unique<DynatraceMetadataFileReaderImpl>();
  return std::make_unique<DynatraceResourceDetector>(proto_config, std::move(reader));
}

/**
 * Static registration for the Dynatrace resource detector factory. @see RegisterFactory.
 */
REGISTER_FACTORY(DynatraceResourceDetectorFactory, ResourceDetectorFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
