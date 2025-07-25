#include "source/extensions/http/early_header_mutation/header_mutation/config.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace HeaderMutation {

Envoy::Http::EarlyHeaderMutationPtr
Factory::createExtension(const Protobuf::Message& message,
                         Server::Configuration::FactoryContext& context) {
  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      *Envoy::Protobuf::DynamicCastMessage<const ProtobufWkt::Any>(&message),
      context.messageValidationVisitor(), *this);
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::http::early_header_mutation::header_mutation::v3::HeaderMutation&>(
      *mptr, context.messageValidationVisitor());
  return std::make_unique<HeaderMutation>(proto_config);
}

REGISTER_FACTORY(Factory, Envoy::Http::EarlyHeaderMutationFactory);

} // namespace HeaderMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
