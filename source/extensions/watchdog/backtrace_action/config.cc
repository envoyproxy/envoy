#include "source/extensions/watchdog/backtrace_action/config.h"

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/extensions/watchdog/backtrace_action/backtrace_action.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace BacktraceAction {

Server::Configuration::GuardDogActionPtr BacktraceActionFactory::createGuardDogActionFromProto(
    const envoy::config::bootstrap::v3::Watchdog::WatchdogAction& config,
    Server::Configuration::GuardDogActionFactoryContext& context) {
  auto message = createEmptyConfigProto();
  THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
      config.config().typed_config(), ProtobufMessage::getStrictValidationVisitor(), *message));
  return std::make_unique<BacktraceAction>(dynamic_cast<BacktraceActionConfig&>(*message), context);
}

/**
 * Static registration for the BacktraceAction factory. @see RegistryFactory.
 */
REGISTER_FACTORY(BacktraceActionFactory, Server::Configuration::GuardDogActionFactory);

} // namespace BacktraceAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
