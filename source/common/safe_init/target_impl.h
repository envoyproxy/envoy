#pragma once

#include <functional>

#include "envoy/safe_init/target.h"

#include "common/common/logger.h"

namespace Envoy {
namespace SafeInit {

/**
 * A TargetHandleImpl functions as a weak reference to a TargetImpl. It is how a ManagerImpl safely
 * tells a target to `initialize` with no guarantees about the target's lifetime.
 */
class TargetHandleImpl : public TargetHandle, Logger::Loggable<Logger::Id::init> {
private:
  friend class TargetImpl;
  TargetHandleImpl(absl::string_view handle_name, absl::string_view name,
                   std::weak_ptr<std::function<void(WatcherHandlePtr)>> fn);

public:
  /**
   * Tell the target to begin initialization, if it is still available.
   * @param watcher A Watcher for the target to notify when it has initialized.
   * @return true if the target received this call, false if the target was already destroyed.
   */
  bool initialize(const Watcher& watcher) const override;

private:
  // Name of the handle (almost always the name of the ManagerImpl calling the target)
  std::string handle_name_;

  // Name of the target
  std::string name_;

  // The target's callback function, only called if the weak pointer can be "locked"
  std::weak_ptr<std::function<void(WatcherHandlePtr)>> fn_;
};

/**
 * A TargetImpl is an entity that can be registered with a Manager for initialization. It can only
 * be invoked through a TargetHandle. Typical usage is as a "mix-in" class:
 *
 *   - Inherit publically from TargetImpl;
 *   - Implement the `initialize` callback method;
 *   - When the target is initialized (either immediately in `initialize` or sometime later), call
 *     `ready` to signal the manager.
 */
class TargetImpl : public Target, Logger::Loggable<Logger::Id::init> {
public:
  /**
   * @param name a human-readable target name, for logging / debugging.
   */
  TargetImpl(absl::string_view name);
  ~TargetImpl() override;

  /**
   * @return a human-readable target name, for logging / debugging.
   */
  absl::string_view name() const override;

  /**
   * Create a new handle that can initialize this target.
   * @param name a human readable handle name, for logging / debugging.
   * @return a new handle that can initialize this target.
   */
  TargetHandlePtr createHandle(absl::string_view handle_name) const override;

  /**
   * Initialize this target. Typically called indirectly via a TargetHandle by the ManagerImpl that
   * the target was registered with.
   */
  virtual void initialize() PURE;

  /**
   * Signal to the init manager that this target has finished initializing. This should ideally
   * only be called once, after `initialize` was called. Calling it before initialization begins
   * or after it has already been called before will have no effect.
   * @return true if the init manager received this call, false otherwise.
   */
  bool ready();

private:
  // Human-readable name for logging
  std::string name_;

  // Handle to the ManagerImpl's internal watcher, to call when this target is initialized
  WatcherHandlePtr watcher_handle_;

  // The callback function, called via TargetHandleImpl by the manager
  std::shared_ptr<std::function<void(WatcherHandlePtr)>> fn_;
};

} // namespace SafeInit
} // namespace Envoy
