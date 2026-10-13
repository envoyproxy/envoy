#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/init/watcher.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Init {

/**
 * A TargetHandle functions as a weak reference to a Target. It is how an implementation of
 * Init::Manager would safely tell a target to `initialize` with no guarantees about the
 * target's lifetime. Typical usage (outside of Init::ManagerImpl) does not require touching
 * TargetHandles at all.
 */
struct TargetHandle {
  virtual ~TargetHandle() = default;

  /**
   * Tell the target to begin initialization, if it is still available.
   * @param watcher A Watcher for the target to notify when it has initialized.
   * @return true if the target received this call, false if the target was already destroyed.
   */
  virtual bool initialize(const Watcher& watcher) const PURE;

  /**
   * @return a human-readable target name, for logging / debugging / tracking target names.
   * The target name has to be unique.
   */
  virtual absl::string_view name() const PURE;
};
using TargetHandlePtr = std::unique_ptr<TargetHandle>;

/**
 * An initialization Target is an entity that can be registered with a Manager for initialization.
 * It can only be invoked through a TargetHandle.
 */
struct Target {
  virtual ~Target() = default;

  /**
   * @return a human-readable target name, for logging / debugging.
   */
  virtual absl::string_view name() const PURE;

  /**
   * Create a new handle that can initialize this target.
   * @param name a human readable handle name, for logging / debugging.
   * @return a new handle that can initialize this target.
   */
  virtual TargetHandlePtr createHandle(absl::string_view name) const PURE;
};

} // namespace Init
} // namespace Envoy
