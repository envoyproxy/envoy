#include "source/extensions/http/original_ip_detection/xff/config.h"

#include "envoy/extensions/http/original_ip_detection/xff/v3/xff.pb.h"
#include "envoy/extensions/http/original_ip_detection/xff/v3/xff.pb.validate.h"
#include "envoy/http/original_ip_detection.h"

#include "source/common/config/utility.h"
#include "source/extensions/http/original_ip_detection/xff/xff.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace Xff {

absl::StatusOr<Envoy::Http::OriginalIPDetectionSharedPtr>
XffIPDetectionFactory::createExtension(const Protobuf::Message& message,
                                       Server::Configuration::FactoryContext& context) {
  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      dynamic_cast<const ProtobufWkt::Any&>(message), context.messageValidationVisitor(), *this);
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::http::original_ip_detection::xff::v3::XffConfig&>(
      *mptr, context.messageValidationVisitor());
  return XffIPDetection::create(proto_config);
}

REGISTER_FACTORY(XffIPDetectionFactory, Envoy::Http::OriginalIPDetectionFactory);

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
