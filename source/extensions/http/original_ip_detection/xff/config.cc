#include "extensions/http/original_ip_detection/xff/config.h"

#include "envoy/extensions/http/original_ip_detection/xff/v3/xff.pb.h"
#include "envoy/extensions/http/original_ip_detection/xff/v3/xff.pb.validate.h"
#include "envoy/http/original_ip_detection.h"
#include "envoy/registry/registry.h"

#include "common/config/utility.h"

#include "extensions/http/original_ip_detection/xff/xff.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace Xff {

Envoy::Http::OriginalIPDetectionSharedPtr
XffIPDetectionFactory::createExtension(const Protobuf::Message& message,
                                       Server::Configuration::FactoryContext& context) {
  // TODO(rgs1): change message's type to TypedExtensionConfig and use translateAnyToFactoryConfig()
  // here along with downcastAndValidate().
  auto config = createEmptyConfigProto();
  Envoy::Config::Utility::translateOpaqueConfig(dynamic_cast<const ProtobufWkt::Any&>(message),
                                                ProtobufWkt::Struct::default_instance(),
                                                context.messageValidationVisitor(), *config);
  const auto& xff_config =
      dynamic_cast<const envoy::extensions::http::original_ip_detection::xff::v3::XffConfig&>(
          *config);
  return std::make_shared<XffIPDetection>(xff_config);
}

REGISTER_FACTORY(XffIPDetectionFactory, Envoy::Http::OriginalIPDetectionFactory);

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
