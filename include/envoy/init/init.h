#pragma once

#include <functional>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Init {

/**
 * A single initialization target. Deprecated, use SafeInit::Target instead.
 * TODO(mergeconflict): convert all Init::Target implementations to SafeInit::TargetImpl.
 */
class Target {
public:
  virtual ~Target() {}

  /**
   * Called when the target should begin its own initialization.
   * @param callback supplies the callback to invoke when the target has completed its
   *        initialization.
   */
  virtual void initialize(std::function<void()> callback) PURE;
};

/**
 * A manager that initializes multiple targets. Deprecated, use SafeInit::Manager instead.
 * TODO(mergeconflict): convert all Init::Manager uses to SafeInit::Manager.
 */
class Manager {
public:
  virtual ~Manager() {}

  /**
   * Register a target to be initialized in the future. The manager will call initialize() on each
   * target at some point in the future. It is an error to register the same target more than once.
   * @param target the Target to initialize.
   * @param description a human-readable description of target used for logging and debugging.
   */
  virtual void registerTarget(Target& target, absl::string_view description) PURE;

  enum class State {
    /**
     * Targets have not been initialized.
     */
    NotInitialized,
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
   * Returns the current state of the init manager.
   */
  virtual State state() const PURE;
};

} // namespace Init
} // namespace Envoy
