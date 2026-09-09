#include "source/extensions/http/early_header_mutation/exact_path_rewrite/config.h"

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/http/early_header_mutation/exact_path_rewrite/header_mutation.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace ExactPathRewrite {

Envoy::Http::EarlyHeaderMutationPtr
Factory::createExtension(const Protobuf::Message& config,
                         Server::Configuration::FactoryContext& context) {
  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      *Envoy::Protobuf::DynamicCastMessage<const Protobuf::Any>(&config),
      context.messageValidationVisitor(), *this);
  const auto& typed_config = MessageUtil::downcastAndValidate<const ProtoExactPathRewrite&>(
      *mptr, context.messageValidationVisitor());
  return THROW_OR_RETURN_VALUE(ExactPathRewrite::create(typed_config),
                               std::unique_ptr<ExactPathRewrite>);
}

REGISTER_FACTORY(Factory, Envoy::Http::EarlyHeaderMutationFactory);

} // namespace ExactPathRewrite
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
