#include "extensions/watchdog/abort_action/config.h"

#include "envoy/registry/registry.h"

#include "common/config/utility.h"
#include "common/protobuf/message_validator_impl.h"

#include "extensions/watchdog/abort_action/abort_action.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace AbortAction {

Server::Configuration::GuardDogActionPtr AbortActionFactory::createGuardDogActionFromProto(
    const envoy::config::bootstrap::v3::Watchdog::WatchdogAction& config,
    Server::Configuration::GuardDogActionFactoryContext& context) {
  auto message = createEmptyConfigProto();
  Config::Utility::translateOpaqueConfig(config.config().typed_config(), ProtobufWkt::Struct(),
                                         ProtobufMessage::getStrictValidationVisitor(), *message);
  return std::make_unique<AbortAction>(dynamic_cast<AbortActionConfig&>(*message), context);
}

/**
 * Static registration for the fixed heap resource monitor factory. @see RegistryFactory.
 */
REGISTER_FACTORY(AbortActionFactory, Server::Configuration::GuardDogActionFactory);

} // namespace AbortAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
