#include "source/common/http/request_id_extension_impl.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Http {

absl::StatusOr<RequestIDExtensionSharedPtr> RequestIDExtensionFactory::fromProto(
    const envoy::extensions::filters::network::http_connection_manager::v3::RequestIDExtension&
        config,
    Server::Configuration::FactoryContext& context) {
  const std::string type{TypeUtil::typeUrlToDescriptorFullName(config.typed_config().type_url())};
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::RequestIDExtensionFactory>::getFactoryByType(
          type);
  if (factory == nullptr) {
    return absl::InvalidArgumentError(
        fmt::format("Didn't find a registered implementation for type: '{}'", type));
  }

  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), context.messageValidationVisitor(), *factory);
  return factory->createExtensionInstance(*message, context);
}

} // namespace Http
} // namespace Envoy
