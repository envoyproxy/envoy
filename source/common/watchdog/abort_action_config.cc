#include "common/watchdog/abort_action_config.h"

#include "envoy/registry/registry.h"

#include "common/config/utility.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/watchdog/abort_action.h"

namespace Envoy {
namespace Watchdog {

Server::Configuration::GuardDogActionPtr AbortActionFactory::createGuardDogActionFromProto(
    const envoy::config::bootstrap::v3::Watchdog::WatchdogAction& config,
    Server::Configuration::GuardDogActionFactoryContext& context) {
  AbortActionConfig message;
  Config::Utility::translateOpaqueConfig(config.config().typed_config(), ProtobufWkt::Struct(),
                                         ProtobufMessage::getStrictValidationVisitor(), message);
  return std::make_unique<AbortAction>(message, context);
}

/**
 * Static registration for the Abort Action factory. @see RegisterFactory.
 */
REGISTER_FACTORY(AbortActionFactory, Server::Configuration::GuardDogActionFactory);

} // namespace Watchdog
} // namespace Envoy
