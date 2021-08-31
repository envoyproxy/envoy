#include "source/common/watchdog/abort_action_config.h"

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/watchdog/abort_action.h"

namespace Envoy {
namespace Watchdog {

Server::Configuration::GuardDogActionPtr AbortActionFactory::createGuardDogActionFromProto(
    const envoy::config::bootstrap::v3::Watchdog::WatchdogAction& config,
    Server::Configuration::GuardDogActionFactoryContext& context) {
  AbortActionConfig message;
  Config::Utility::translateOpaqueConfig(config.config().typed_config(),
                                         ProtobufMessage::getStrictValidationVisitor(), message);
  return std::make_unique<AbortAction>(message, context);
}

/**
 * Static registration for the Abort Action factory. @see RegisterFactory.
 */
REGISTER_FACTORY(AbortActionFactory, Server::Configuration::GuardDogActionFactory);

} // namespace Watchdog
} // namespace Envoy
