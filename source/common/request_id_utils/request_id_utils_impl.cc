#include "common/request_id_utils/request_id_utils_impl.h"

#include "common/common/utility.h"
#include "common/config/utility.h"

namespace Envoy {
namespace RequestIDUtils {

std::string RequestIDUtilsFactory::defaultFactoryName() { return "envoy.request_id_utils.uuid"; }
/**
 * Request ID Utilities factory that reads the configuration from proto.
 */
UtilitiesSharedPtr RequestIDUtilsFactory::byName(const std::string& config,
                                                 Server::Configuration::FactoryContext& context) {

  auto& factory =
      Config::Utility::getAndCheckFactoryByName<Server::Configuration::RequestIDUtilsFactory>(
          config);
  return factory.createUtilitiesInstance(context);
}

UtilitiesSharedPtr
RequestIDUtilsFactory::defaultInstance(Server::Configuration::FactoryContext& context) {
  return byName(defaultFactoryName(), context);
}

} // namespace RequestIDUtils
} // namespace Envoy
