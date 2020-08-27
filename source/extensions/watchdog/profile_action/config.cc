#include "extensions/watchdog/profile_action/config.h"

#include "envoy/registry/registry.h"

#include "common/config/utility.h"
#include "common/protobuf/message_validator_impl.h"

#include "extensions/watchdog/profile_action/profile_action.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace ProfileAction {

Server::Configuration::GuardDogActionPtr ProfileActionFactory::createGuardDogActionFromProto(
    const envoy::config::bootstrap::v3::Watchdog::WatchdogAction& config,
    Server::Configuration::GuardDogActionFactoryContext& context) {
  auto message = createEmptyConfigProto();
  Config::Utility::translateOpaqueConfig(config.config().typed_config(), ProtobufWkt::Struct(),
                                         ProtobufMessage::getStrictValidationVisitor(), *message);
  return std::make_unique<ProfileAction>(dynamic_cast<ProfileActionConfig&>(*message), context);
}

/**
 * Static registration for the ProfileAction factory. @see RegistryFactory.
 */
REGISTER_FACTORY(ProfileActionFactory, Server::Configuration::GuardDogActionFactory);

} // namespace ProfileAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
