#pragma once

#include <memory>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/event/dispatcher.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/instance.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class FatalAction {
public:
  virtual ~FatalAction() = default;
  /**
   * Callback function to run when Envoy is crashing.
   * @param tracked_objects a span of objects Envoy was working on when Envoy started crashing.
   */
  virtual void run(absl::Span<const ScopeTrackedObject* const> tracked_objects) PURE;

  /**
   * @return whether the action is async-signal-safe.
   * See man 7 signal-safety for the definition of async-signal-safe.
   */
  virtual bool isAsyncSignalSafe() const PURE;
};

using FatalActionPtr = std::unique_ptr<FatalAction>;

/**
 * Implemented by each custom FatalAction and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class FatalActionFactory : public Config::TypedFactory {
public:
  ~FatalActionFactory() override = default;

  /**
   * Creates a particular FatalAction implementation.
   *
   * @param config supplies the configuration for the action.
   * @param context supplies the GuardDog Action's context.
   * @return FatalActionsPtr the FatalActions object.
   */
  virtual FatalActionPtr
  createFatalActionFromProto(const envoy::config::bootstrap::v3::FatalAction& config,
                             Instance* server) PURE;

  std::string category() const override { return "envoy.fatal_action"; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
