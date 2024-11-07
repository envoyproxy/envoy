#include "source/extensions/http/original_ip_detection/custom_header/config.h"

#include "envoy/extensions/http/original_ip_detection/custom_header/v3/custom_header.pb.h"
#include "envoy/extensions/http/original_ip_detection/custom_header/v3/custom_header.pb.validate.h"
#include "envoy/http/original_ip_detection.h"

#include "source/common/config/utility.h"
#include "source/extensions/http/original_ip_detection/custom_header/custom_header.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace CustomHeader {

absl::StatusOr<Envoy::Http::OriginalIPDetectionSharedPtr>
CustomHeaderIPDetectionFactory::createExtension(const Protobuf::Message& message,
                                                Server::Configuration::FactoryContext& context) {
  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      *Envoy::Protobuf::DynamicCastMessage<const ProtobufWkt::Any>(&message),
      context.messageValidationVisitor(), *this);
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::http::original_ip_detection::custom_header::v3::CustomHeaderConfig&>(
      *mptr, context.messageValidationVisitor());
  return std::make_shared<CustomHeaderIPDetection>(proto_config);
}

REGISTER_FACTORY(CustomHeaderIPDetectionFactory, Envoy::Http::OriginalIPDetectionFactory);

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
