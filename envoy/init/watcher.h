#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Init {

/**
 * A WatcherHandle functions as a weak reference to a Watcher. It is how an implementation of
 * Init::Target would safely notify a Manager that it has initialized, and likewise it's how
 * an implementation of Init::Manager would safely tell its client that all registered targets
 * have initialized, with no guarantees about the lifetimes of the manager or client. Typical usage
 * (outside of Init::TargetImpl and ManagerImpl) does not require touching WatcherHandles at
 * all.
 */
struct WatcherHandle {
  virtual ~WatcherHandle() = default;

  /**
   * Tell the watcher that initialization has completed, if it is still available.
   * @return true if the watcher received this call, false if the watcher was already destroyed.
   */
  virtual bool ready() const PURE;
};
using WatcherHandlePtr = std::unique_ptr<WatcherHandle>;

/**
 * A Watcher is an entity that listens for notifications that either an initialization target or
 * all targets registered with a manager have initialized. It can only be invoked through a
 * WatcherHandle.
 */
struct Watcher {
  virtual ~Watcher() = default;

  /**
   * @return a human-readable target name, for logging / debugging.
   */
  virtual absl::string_view name() const PURE;

  /**
   * Create a new handle that can notify this watcher.
   * @param name a human readable handle name, for logging / debugging.
   * @return a new handle that can notify this watcher.
   */
  virtual WatcherHandlePtr createHandle(absl::string_view name) const PURE;
};

} // namespace Init
} // namespace Envoy
