#include "common/request_id_extension/request_id_extension_impl.h"

#include "common/common/utility.h"
#include "common/config/utility.h"
#include "common/request_id_extension/uuid_impl.h"

namespace Envoy {
namespace RequestIDExtension {

UtilitiesSharedPtr RequestIDExtensionFactory::fromProto(
    const envoy::extensions::filters::network::http_connection_manager::v3::RequestIDExtension&
        config,
    Server::Configuration::FactoryContext& context) {
  const std::string type{TypeUtil::typeUrlToDescriptorFullName(config.typed_config().type_url())};
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::RequestIDExtensionFactory>::getFactoryByType(
          type);
  if (factory == nullptr) {
    throw EnvoyException(
        fmt::format("Didn't find a registered implementation for type: '{}'", type));
  }

  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), context.messageValidationVisitor(), *factory);
  return factory->createUtilitiesInstance(*message, context);
}

UtilitiesSharedPtr
RequestIDExtensionFactory::defaultInstance(Envoy::Runtime::RandomGenerator& random) {
  return std::make_shared<UUIDUtils>(random);
}

} // namespace RequestIDExtension
} // namespace Envoy
