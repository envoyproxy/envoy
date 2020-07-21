#pragma once

#include "envoy/api/api.h"
#include "envoy/common/pure.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/guarddog.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

struct GuardDogActionFactoryContext {
  // TODO(kbaichoo): Fill this with useful context that might be used
  int foo;
};

/**
 * Implemented by each custom GuardDogAction and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class GuardDogActionFactory : public Config::TypedFactory {
public:
  ~GuardDogActionFactory() override = default;

  /**
   * Creates a particular GuardDog Action factory implementation.
   *
   * @param config supplies the configuration for the action.
   * @param context supplies the GuardDog Action's context.
   * @return GuardDogActionCb the callback for the action.
   */
  virtual GuardDogActionCb createGuardDogActionFromProto(
      const envoy::config::bootstrap::v3::Watchdog::WatchdogAction& config,
      GuardDogActionFactoryContext& context) PURE;

  std::string category() const override { return "envoy.guarddog_actions"; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
