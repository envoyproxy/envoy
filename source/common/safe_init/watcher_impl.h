#pragma once

#include <functional>

#include "envoy/safe_init/watcher.h"

#include "common/common/logger.h"

namespace Envoy {
namespace SafeInit {

/**
 * A WatcherHandleImpl functions as a weak reference to a Watcher. It is how a TargetImpl safely
 * notifies a ManagerImpl that it has initialized, and likewise it's how ManagerImpl safely tells
 * its client that all registered targets have initialized, with no guarantees about the lifetimes
 * of the manager or client.
 */
class WatcherHandleImpl : public WatcherHandle, Logger::Loggable<Logger::Id::init> {
private:
  friend class WatcherImpl;
  WatcherHandleImpl(absl::string_view handle_name, absl::string_view name,
                    std::weak_ptr<std::function<void()>> fn);

public:
  /**
   * Tell the watcher that initialization has completed, if it is still available.
   * @return true if the watcher received this call, false if the watcher was already destroyed.
   */
  bool ready() const override;

private:
  // Name of the handle (either the name of the target calling the manager, or the name of the
  // manager calling the client)
  std::string handle_name_;

  // Name of the watcher (either the name of the manager, or the name of the client)
  std::string name_;

  // The watcher's callback function, only called if the weak pointer can be "locked"
  std::weak_ptr<std::function<void()>> fn_;
};

/**
 * A WatcherImpl is an entity that listens for notifications that either an initialization target or
 * all targets registered with a manager have initialized. It can only be invoked through a
 * WatcherHandleImpl. Typical usage is as a data member, initialized with a lambda.
 */
class WatcherImpl : public Watcher, Logger::Loggable<Logger::Id::init> {
public:
  /**
   * @param name a human-readable watcher name, for logging / debugging
   * @param fn a callback function to invoke when `ready` is called on the handle
   */
  WatcherImpl(absl::string_view name, std::function<void()> fn);
  ~WatcherImpl() override;

  /**
   * @return a human-readable target name, for logging / debugging.
   */
  absl::string_view name() const override;

  /**
   * Create a new handle that can notify this watcher.
   * @param name a human readable handle name, for logging / debugging.
   * @return a new handle that can notify this watcher.
   */
  WatcherHandlePtr createHandle(absl::string_view handle_name) const override;

private:
  // Human-readable name for logging
  std::string name_;

  // The callback function, called via WatcherHandleImpl by either the target or the manager
  std::shared_ptr<std::function<void()>> fn_;
};

} // namespace SafeInit
} // namespace Envoy
