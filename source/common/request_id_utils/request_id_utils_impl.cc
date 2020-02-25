#include "common/request_id_utils/request_id_utils_impl.h"

#include "common/common/utility.h"
#include "common/config/utility.h"

#include "extensions/request_id_utils/uuid/config.h"

namespace Envoy {
namespace RequestIDUtils {

UtilitiesSharedPtr RequestIDUtilsFactory::fromProto(
    const envoy::extensions::filters::network::http_connection_manager::v3::RequestIDUtils& config,
    Server::Configuration::FactoryContext& context) {

  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::RequestIDUtilsFactory>(config);
  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      config, context.messageValidationVisitor(), factory);
  return factory.createUtilitiesInstance(*message, context);
}

UtilitiesSharedPtr
RequestIDUtilsFactory::defaultInstance(Server::Configuration::FactoryContext& context) {
  return Envoy::Extensions::RequestIDUtils::UUID::UUIDUtilsFactory().createUtilitiesInstance(
      ProtobufWkt::Empty(), context);
}

} // namespace RequestIDUtils
} // namespace Envoy
