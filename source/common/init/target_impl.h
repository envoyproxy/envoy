#pragma once

#include <functional>

#include "envoy/init/target.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Init {

/**
 * A target is just a glorified callback function, called by the manager it was registered with.
 */
using InitializeFn = std::function<void()>;

/**
 * Internally, the callback is slightly more sophisticated: it actually takes a WatcherHandlePtr
 * that it uses to notify the manager when the target is ready. It saves this pointer when invoked
 * and resets it later in `ready`. Users needn't care about this implementation detail, they only
 * need to provide an `InitializeFn` above when constructing a target.
 */
using InternalInitalizeFn = std::function<void(WatcherHandlePtr)>;

/**
 * A TargetHandleImpl functions as a weak reference to a TargetImpl. It is how a ManagerImpl safely
 * tells a target to `initialize` with no guarantees about the target's lifetime.
 */
class TargetHandleImpl : public TargetHandle, Logger::Loggable<Logger::Id::init> {
private:
  friend class AbstractTarget;
  TargetHandleImpl(absl::string_view handle_name, absl::string_view name,
                   std::weak_ptr<InternalInitalizeFn> fn);

public:
  // Init::TargetHandle
  bool initialize(const Watcher& watcher) const override;

private:
  // Name of the handle (almost always the name of the ManagerImpl calling the target)
  const std::string handle_name_;

  // Name of the target
  const std::string name_;

  // The target's callback function, only called if the weak pointer can be "locked"
  const std::weak_ptr<InternalInitalizeFn> fn_;
};

class AbstractTarget : public Target, protected Logger::Loggable<Logger::Id::init> {
public:
  AbstractTarget(absl::string_view name, std::shared_ptr<InternalInitalizeFn> fn);

  // Init::Target
  absl::string_view name() const override;
  TargetHandlePtr createHandle(absl::string_view handle_name) const override;

  virtual bool ready() PURE;

protected:
  // Human-readable name for logging
  const std::string name_;

  // Handle to the ManagerImpl's internal watcher, to call when this target is initialized
  WatcherHandlePtr watcher_handle_;

  // The callback function, called via TargetHandleImpl by the manager
  const std::shared_ptr<InternalInitalizeFn> fn_;
};

/**
 * A TargetImpl is an entity that can be registered with a Manager for initialization. It can only
 * be invoked through a TargetHandle.
 */
class TargetImpl : public AbstractTarget {
public:
  /**
   * @param name a human-readable target name, for logging / debugging
   * @fn a callback function to invoke when `initialize` is called on the handle. Note that this
   *     doesn't take a WatcherHandlePtr (like TargetFn does). Managing the watcher handle is done
   *     internally to simplify usage.
   */
  TargetImpl(absl::string_view name, InitializeFn fn);
  ~TargetImpl() override;

  /**
   * Signal to the init manager that this target has finished initializing. This is safe to call
   * any time.
   * Notes: Calling it before initialization begins is allowed.
   *        Calling it after initialization has already ended will have no effect.
   * @return true if the init manager received this call, false otherwise.
   */
  bool ready() override;
};
/**
 * A TargetImpl is an entity that can be registered with a Manager for initialization. It can only
 * be invoked through a TargetHandle.
 */
class EagerTargetImpl : public AbstractTarget {
public:
  /**
   * @param name a human-readable target name, for logging / debugging
   * @fn a callback function to invoke when `initialize` is called on the handle. Note that this
   *     doesn't take a WatcherHandlePtr (like TargetFn does). Managing the watcher handle is done
   *     internally to simplify usage.
   */
  EagerTargetImpl(absl::string_view name, InitializeFn fn);
  ~EagerTargetImpl() override;

  /**
   * Signal to the init manager that this target has finished initializing. This is safe to call
   * any time.
   * Notes: Calling it before initialization begins is allowed.
   *        Calling it after initialization has already ended will have no effect.
   * @return true if the init manager received this call, false otherwise.
   */
  bool ready() override;

private:
  // True if this target is currently ready
  bool is_ready_{};
};

} // namespace Init
} // namespace Envoy
