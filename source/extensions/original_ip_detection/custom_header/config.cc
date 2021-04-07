#include "extensions/original_ip_detection/custom_header/config.h"

#include "envoy/extensions/original_ip_detection/custom_header/v3/custom_header.pb.h"
#include "envoy/extensions/original_ip_detection/custom_header/v3/custom_header.pb.validate.h"
#include "envoy/http/original_ip_detection.h"
#include "envoy/registry/registry.h"

#include "common/config/utility.h"

#include "extensions/original_ip_detection/custom_header/custom_header.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace CustomHeader {

Http::OriginalIPDetectionSharedPtr
CustomHeaderIPDetectionFactory::createExtension(const Protobuf::Message& message,
                                                Server::Configuration::FactoryContext& context) {
  // TODO(rgs1): change message's type to TypedExtensionConfig and use translateAnyToFactoryConfig()
  // here along with downcastAndValidate().
  auto config = createEmptyConfigProto();
  Envoy::Config::Utility::translateOpaqueConfig(dynamic_cast<const ProtobufWkt::Any&>(message),
                                                ProtobufWkt::Struct::default_instance(),
                                                context.messageValidationVisitor(), *config);
  const auto& custom_header_config = dynamic_cast<
      const envoy::extensions::original_ip_detection::custom_header::v3::CustomHeaderConfig&>(
      *config);
  return std::make_shared<CustomHeaderIPDetection>(custom_header_config);
}

REGISTER_FACTORY(CustomHeaderIPDetectionFactory, Http::OriginalIPDetectionFactory);

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
