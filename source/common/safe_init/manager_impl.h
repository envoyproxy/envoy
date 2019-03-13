#pragma once

#include <list>

#include "envoy/safe_init/manager.h"

#include "common/common/logger.h"
#include "common/safe_init/watcher_impl.h"

namespace Envoy {
namespace SafeInit {

/**
 * SafeInit::ManagerImpl coordinates initialization of one or more "targets." See comments in
 * include/envoy/safe_init/manager.h for an overview.
 *
 * When the logging level is set to "debug" or "trace," the log will contain entries for all
 * significant events in the initialization flow:
 *
 *   - Targets added to the manager
 *   - Initialization started for the manager and for each target
 *   - Initialization completed for each target and for the manager
 *   - Destruction of targets and watchers
 *   - Callbacks to "unavailable" (deleted) targets, manager, or watchers
 */
class ManagerImpl : public Manager, Logger::Loggable<Logger::Id::init> {
public:
  /**
   * @param name a human-readable manager name, for logging / debugging.
   */
  ManagerImpl(absl::string_view name);

  /**
   * @return the current state of the manager.
   */
  State state() const override;

  /**
   * Register an initialization target. If the manager's current state is uninitialized, the target
   * will be saved for invocation later, when `initialize` is called. If the current state is
   * initializing, the target will be invoked immediately. It is an error to register a target with
   * a manager that is already in initialized state.
   * @param target the target to be invoked when initialization begins.
   */
  void add(const Target& target) override;

  /**
   * Start initialization of all previously registered targets, and notify the given Watcher when
   * initialization is complete. It is an error to call initialize on a manager that is already in
   * initializing or initialized state. If the manager contains no targets, initialization completes
   * immediately.
   * @param watcher the watcher to notify when initialization is complete.
   */
  void initialize(const Watcher& watcher) override;

private:
  // Human-readable name for logging
  std::string name_;

  // Current state
  State state_;

  // Current number of registered targets that have not yet initialized
  uint32_t count_;

  // Handle to the watcher passed in `initialize`, to be called when initialization completes
  WatcherHandlePtr watcher_handle_;

  // Watcher to receive ready notifications from each target
  WatcherImpl watcher_;

  // All registered targets
  std::list<TargetHandlePtr> target_handles_;
};

} // namespace SafeInit
} // namespace Envoy
