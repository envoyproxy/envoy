#include "common/request_id_utils/request_id_utils_impl.h"

#include "common/common/utility.h"
#include "common/config/utility.h"

#include "extensions/request_id_utils/uuid/config.h"

namespace Envoy {
namespace RequestIDUtils {

UtilitiesSharedPtr RequestIDUtilsFactory::fromProto(
    const envoy::extensions::filters::network::http_connection_manager::v3::RequestIDUtils& config,
    Server::Configuration::FactoryContext& context) {
  const std::string type{TypeUtil::typeUrlToDescriptorFullName(config.typed_config().type_url())};
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::RequestIDUtilsFactory>::getFactoryByType(
          type);
  if (factory == nullptr) {
    throw EnvoyException(
        fmt::format("Didn't find a registered implementation for type: '{}'", type));
  }

  // FIXME(euroelessar): Use Config::Utility::translateAnyToFactoryConfig once
  // https://github.com/envoyproxy/envoy/pull/10418 is merged.
  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  Config::Utility::translateOpaqueConfig(config.typed_config(), ProtobufWkt::Struct(),
                                         context.messageValidationVisitor(), *message);
  return factory->createUtilitiesInstance(*message, context);
}

UtilitiesSharedPtr
RequestIDUtilsFactory::defaultInstance(Server::Configuration::FactoryContext& context) {
  return Envoy::Extensions::RequestIDUtils::UUID::UUIDUtilsFactory().createUtilitiesInstance(
      ProtobufWkt::Empty(), context);
}

} // namespace RequestIDUtils
} // namespace Envoy
