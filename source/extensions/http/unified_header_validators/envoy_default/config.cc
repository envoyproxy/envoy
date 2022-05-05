#include "source/extensions/http/unified_header_validators/envoy_default/config.h"

#include "envoy/extensions/http/unified_header_validators/envoy_default/v3/unified_header_validator.pb.h"
#include "envoy/extensions/http/unified_header_validators/envoy_default/v3/unified_header_validator.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/extensions/http/unified_header_validators/envoy_default/unified_header_validator.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace UnifiedHeaderValidators {
namespace EnvoyDefault {

::Envoy::Http::UnifiedHeaderValidatorFactorySharedPtr
UnifiedHeaderValidatorFactoryConfig::createFromProto(
    const Protobuf::Message& message, Server::Configuration::FactoryContext& context) {
  auto mptr = ::Envoy::Config::Utility::translateAnyToFactoryConfig(
      dynamic_cast<const ProtobufWkt::Any&>(message), context.messageValidationVisitor(), *this);
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const ::envoy::extensions::http::unified_header_validators::
                                           envoy_default::v3::UnifiedHeaderValidatorConfig&>(
          *mptr, context.messageValidationVisitor());
  return std::make_shared<UnifiedHeaderValidatorFactory>(proto_config);
}

ProtobufTypes::MessagePtr UnifiedHeaderValidatorFactoryConfig::createEmptyConfigProto() {
  return std::make_unique<::envoy::extensions::http::unified_header_validators::envoy_default::v3::
                              UnifiedHeaderValidatorConfig>();
}

REGISTER_FACTORY(UnifiedHeaderValidatorFactoryConfig,
                 ::Envoy::Http::UnifiedHeaderValidatorFactoryConfig);

} // namespace EnvoyDefault
} // namespace UnifiedHeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
