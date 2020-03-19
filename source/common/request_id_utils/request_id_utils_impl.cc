#include "common/request_id_utils/request_id_utils_impl.h"

#include "common/common/utility.h"
#include "common/config/utility.h"
#include "common/request_id_utils/uuid_impl.h"

namespace Envoy {
namespace RequestIDUtils {

UtilitiesSharedPtr RequestIDUtilsFactory::fromProto(
    const envoy::extensions::filters::network::http_connection_manager::v3::RequestIDExtension&
        config,
    Server::Configuration::FactoryContext& context) {
  const std::string type{TypeUtil::typeUrlToDescriptorFullName(config.typed_config().type_url())};
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::RequestIDUtilsFactory>::getFactoryByType(
          type);
  if (factory == nullptr) {
    throw EnvoyException(
        fmt::format("Didn't find a registered implementation for type: '{}'", type));
  }

  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), context.messageValidationVisitor(), *factory);
  return factory->createUtilitiesInstance(*message, context);
}

UtilitiesSharedPtr RequestIDUtilsFactory::defaultInstance(Envoy::Runtime::RandomGenerator& random) {
  return std::make_shared<UUIDUtils>(random);
}

} // namespace RequestIDUtils
} // namespace Envoy
