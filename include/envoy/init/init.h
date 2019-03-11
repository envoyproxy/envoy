#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace Init {

/**
 * Implementation-defined representations of initialization callbacks (see e.g.
 * /source/init/callback.h). A TargetReceiver is called by the init manager to signal the target
 * should begin initialization, and a Receiver is called by the init manager when initialization of
 * all targets is complete.
 */
class TargetReceiver;
class Receiver;

/**
 * Init::Manager coordinates initialization of one or more "targets." A target registers its need
 * for initialization by passing a TargetReceiver to `add`. When `initialize` is called on the
 * manager, it notifies all targets to initialize.
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
   * @param target_receiver the target to be invoked when initialization begins.
   */
  virtual void add(const TargetReceiver& target_receiver) PURE;

  /**
   * Start initialization of all previously registered targets. It is an error to call initialize
   * on a manager that is already in initializing or initialized state.
   */
  virtual void initialize(const Receiver& receiver) PURE;
};

} // namespace Init
} // namespace Envoy
