#include "source/extensions/http/header_validators/envoy_default/config.h"

#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/extensions/http/header_validators/envoy_default/header_validator_factory.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

::Envoy::Http::HeaderValidatorFactoryPtr
HeaderValidatorFactoryConfig::createFromProto(const Protobuf::Message& message,
                                              Server::Configuration::FactoryContext& context) {
  auto mptr = ::Envoy::Config::Utility::translateAnyToFactoryConfig(
      dynamic_cast<const ProtobufWkt::Any&>(message), context.messageValidationVisitor(), *this);
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const ::envoy::extensions::http::header_validators::
                                           envoy_default::v3::HeaderValidatorConfig&>(
          *mptr, context.messageValidationVisitor());
  return std::make_unique<HeaderValidatorFactory>(proto_config);
}

ProtobufTypes::MessagePtr HeaderValidatorFactoryConfig::createEmptyConfigProto() {
  return std::make_unique<
      ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig>();
}

REGISTER_FACTORY(HeaderValidatorFactoryConfig, ::Envoy::Http::HeaderValidatorFactoryConfig);

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
