#pragma once

#include <memory>

#include "envoy/api/api.h"
#include "envoy/common/pure.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/event/dispatcher.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/guarddog.h"
#include "envoy/stats/scope.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

struct GuardDogActionFactoryContext {
  Api::Api& api_;
  Event::Dispatcher& dispatcher_; // not owned (this is the guard dog's dispatcher)
  Stats::Scope& stats_;           // not owned (this is the server's stats scope)
  absl::string_view guarddog_name_;
};

class GuardDogAction {
public:
  virtual ~GuardDogAction() = default;
  /**
   * Callback function for when the GuardDog observes an event.
   * @param event the event the GuardDog observes.
   * @param thread_last_checkin_pairs pair of the relevant thread to the event, and the
   *  last check in time of those threads with their watchdog.
   * @param now the current time.
   */
  virtual void
  run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::WatchdogEvent event,
      const std::vector<std::pair<Thread::ThreadId, MonotonicTime>>& thread_last_checkin_pairs,
      MonotonicTime now) PURE;
};

using GuardDogActionPtr = std::unique_ptr<GuardDogAction>;

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
   * @return GuardDogActionPtr the GuardDogAction object.
   */
  virtual GuardDogActionPtr createGuardDogActionFromProto(
      const envoy::config::bootstrap::v3::Watchdog::WatchdogAction& config,
      GuardDogActionFactoryContext& context) PURE;

  std::string category() const override { return "envoy.guarddog_actions"; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
