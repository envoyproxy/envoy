#pragma once

#include <functional>

#include "envoy/safe_init/target.h"

#include "common/common/logger.h"

namespace Envoy {
namespace SafeInit {

// A target is just a glorified callback function that accepts a watcher handle.
using TargetFn = std::function<void(WatcherHandlePtr)>;

/**
 * A TargetHandleImpl functions as a weak reference to a TargetImpl. It is how a ManagerImpl safely
 * tells a target to `initialize` with no guarantees about the target's lifetime.
 */
class TargetHandleImpl : public TargetHandle, Logger::Loggable<Logger::Id::init> {
private:
  friend class TargetImpl;
  TargetHandleImpl(absl::string_view handle_name, absl::string_view name,
                   std::weak_ptr<TargetFn> fn);

public:
  // SafeInit::TargetHandle
  bool initialize(const Watcher& watcher) const override;

private:
  // Name of the handle (almost always the name of the ManagerImpl calling the target)
  const std::string handle_name_;

  // Name of the target
  const std::string name_;

  // The target's callback function, only called if the weak pointer can be "locked"
  const std::weak_ptr<TargetFn> fn_;
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

  // SafeInit::Target
  absl::string_view name() const override;
  TargetHandlePtr createHandle(absl::string_view handle_name) const override;

  /**
   * Initialize this target. Typically called indirectly via a TargetHandle by the ManagerImpl that
   * the target was registered with.
   */
  virtual void initialize() PURE;

  /**
   * Signal to the init manager that this target has finished initializing. This is safe to call
   * any time. Calling it before initialization begins or after initialization has already ended
   * will have no effect.
   * @return true if the init manager received this call, false otherwise.
   */
  bool ready();

private:
  void onInitialize(WatcherHandlePtr watcher_handle);

  // Human-readable name for logging
  const std::string name_;

  // Handle to the ManagerImpl's internal watcher, to call when this target is initialized
  WatcherHandlePtr watcher_handle_;

  // The callback function, called via TargetHandleImpl by the manager
  const std::shared_ptr<TargetFn> fn_;
};

} // namespace SafeInit
} // namespace Envoy
