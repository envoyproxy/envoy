#include "source/extensions/http/original_ip_detection/extracted_external_address/config.h"

#include "envoy/extensions/http/original_ip_detection/extracted_external_address/v3/extracted_external_address.pb.h"
#include "envoy/extensions/http/original_ip_detection/extracted_external_address/v3/extracted_external_address.pb.validate.h"
#include "envoy/http/original_ip_detection.h"

#include "source/common/config/utility.h"
#include "source/extensions/http/original_ip_detection/extracted_external_address/extracted_external_address.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace ExtractedExternalAddress {

absl::StatusOr<Envoy::Http::OriginalIPDetectionSharedPtr>
ExtractedExternalAddressIPDetectionFactory::createExtension(
    const Protobuf::Message& message, Server::Configuration::FactoryContext& context) {
  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      *Envoy::Protobuf::DynamicCastMessage<const Protobuf::Any>(&message),
      context.messageValidationVisitor(), *this);
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::http::original_ip_detection::
                                           extracted_external_address::v3::
                                               ExtractedExternalAddressConfig&>(
          *mptr, context.messageValidationVisitor());
  return std::make_shared<ExtractedExternalAddressIPDetection>(proto_config);
}

REGISTER_FACTORY(ExtractedExternalAddressIPDetectionFactory,
                 Envoy::Http::OriginalIPDetectionFactory);

} // namespace ExtractedExternalAddress
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
