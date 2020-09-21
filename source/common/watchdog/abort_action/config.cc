#include "common/watchdog/abort_action/config.h"

#include "envoy/registry/registry.h"

#include "common/config/utility.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/watchdog/abort_action/abort_action.h"

namespace Envoy {
namespace Watchdog {
namespace AbortAction {

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

} // namespace AbortAction
} // namespace Watchdog
} // namespace Envoy
