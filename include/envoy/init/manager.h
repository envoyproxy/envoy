#pragma once

#include "envoy/admin/v3/init_dump.pb.h"
#include "envoy/common/pure.h"
#include "envoy/init/target.h"
#include "envoy/init/watcher.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Init {

/**
 * Init::Manager coordinates initialization of one or more "targets." A typical flow would be:
 *
 *   - One or more initialization targets are registered with a manager using `add`.
 *   - The manager is told to `initialize` all its targets, given a Watcher to notify when all
 *     registered targets are initialized.
 *   - Each target will initialize, either immediately or asynchronously, and will signal
 *     `ready` to the manager when initialized.
 *   - When all targets are initialized, the manager signals `ready` to the watcher it was given
 *     previously.
 *
 * Since there are several entities involved in this flow -- the owner of the manager, the targets
 * registered with the manager, and the manager itself -- it may be difficult or impossible in some
 * cases to guarantee that their lifetimes line up correctly to avoid use-after-free errors. The
 * interface design here in Init allows implementations to avoid the issue:
 *
 *   - A Target can only be initialized via a TargetHandle, which acts as a weak reference.
 *     Attempting to initialize a destroyed Target via its handle has no ill effects.
 *   - Likewise, a Watcher can only be notified that initialization was complete via a
 *     WatcherHandle, which acts as a weak reference as well.
 *
 * See target.h and watcher.h, as well as implementation in source/common/init for details.
 */
struct Manager {
  virtual ~Manager() = default;

  /**
   * The manager's state, used e.g. for reporting in the admin server.
   */
  enum class State {
    /**
     * Targets have not been initialized.
     */
    Uninitialized,
    /**
     * Targets are currently being initialized.
     */
    Initializing,
    /**
     * All targets have been initialized.
     */
    Initialized
  };

  /**
   * @return the current state of the manager.
   */
  virtual State state() const PURE;

  /**
   * Register an initialization target. If the manager's current state is uninitialized, the target
   * will be saved for invocation later, when `initialize` is called. If the current state is
   * initializing, the target will be invoked immediately. It is an error to register a target with
   * a manager that is already in initialized state.
   * @param target the target to be invoked when initialization begins.
   */
  virtual void add(const Target& target) PURE;

  /**
   * Start initialization of all previously registered targets, and notify the given Watcher when
   * initialization is complete. It is an error to call initialize on a manager that is already in
   * initializing or initialized state. If the manager contains no targets, initialization completes
   * immediately.
   * @param watcher the watcher to notify when initialization is complete.
   */
  virtual void initialize(const Watcher& watcher) PURE;

  /**
   * Add unready targets information into the config dump.
   */
  virtual void dumpUnreadyTargets(envoy::admin::v3::UnreadyTargetsDumps& dumps) PURE;
};

} // namespace Init
} // namespace Envoy
