#pragma once

#include "envoy/init/init.h"

#include "common/callback/manager.h"
#include "common/common/logger.h"

#include "absl/strings/string_view.h"
#include "init/callback.h"

namespace Envoy {
namespace Init {

/**
 * Init::Manager coordinates asynchronous initialization of one or more "targets" using the safe
 * callback mechanism defined in Envoy::Common::Callback. A target registers its need for
 * initialization by passing a TargetReceiver to `add`. When `initialize` is called on the manager,
 * it calls all targets, passing each a Caller to invoke when its initialization is complete. When
 * all targets are finished initializing, the manager will finally notify its client.
 *
 * Note that it's safe for an initialization target to invoke a Caller from a destroyed manager,
 * and likewise for the manager to invoke a Caller from a destroyed client. This does happen in
 * practice, for example when a warming listener is destroyed before its route configuration is
 * received (see issue #6116).
 */
class ManagerImpl : public Manager, Logger::Loggable<Logger::Id::init> {
public:
  /**
   * Constructs an initialization manager for a given caller.
   * @param name human-readable name of the init manager for tracing.
   */
  ManagerImpl(absl::string_view name);

  /**
   * @return the current state of the manager.
   */
  State state() const override;

  /**
   * Register an initialization target. If the manager's current state is uninitialized, the target
   * will be saved for invocation later, when initialize is called. If the current state is
   * initializing, the target will be invoked immediately. It is an error to register a target with
   * a manager that is already in initialized state.
   * @param target_receiver the target to be invoked when initialization begins.
   */
  void add(const TargetReceiver& target_receiver) override;

  /**
   * Start initialization of all previously registered targets. It is an error to call initialize
   * on a manager that is already in initializing or initialized state.
   * @param receiver callback to be invoked when initialization of all targets is complete.
   */
  void initialize(const Receiver& receiver) override;

private:
  std::string name_;
  State state_;
  uint32_t count_;
  Caller caller_;
  Receiver receiver_;
  Common::Callback::ManagerT<TargetCaller, const Receiver&> targets_;
};

} // namespace Init
} // namespace Envoy
