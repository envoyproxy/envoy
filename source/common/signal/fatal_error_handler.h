#pragma once

#include <ostream>

#include "envoy/common/pure.h"

#include "common/signal/fatal_action.h"

namespace Envoy {

// A simple class which allows registering functions to be called when Envoy
// receives one of the fatal signals, documented in signal_action.h.
class FatalErrorHandlerInterface {
public:
  virtual ~FatalErrorHandlerInterface() = default;
  // Called when Envoy receives a fatal signal. Must be async-signal-safe: in
  // particular, it can't allocate memory.
  virtual void onFatalError(std::ostream& os) const PURE;

  virtual void
  runFatalActionsOnTrackedObject(const FatalAction::FatalActionPtrList& actions) const PURE;
};

namespace FatalErrorHandler {
/**
 * Add this handler to the list of functions which will be called if Envoy
 * receives a fatal signal.
 */
void registerFatalErrorHandler(const FatalErrorHandlerInterface& handler);

/**
 * Removes this handler from the list of functions which will be called if Envoy
 * receives a fatal signal.
 */
void removeFatalErrorHandler(const FatalErrorHandlerInterface& handler);

/**
 * Calls and unregisters the fatal error handlers registered with
 * registerFatalErrorHandler. This is async-signal-safe and intended to be
 * called from a fatal signal handler.
 */
void callFatalErrorHandlers(std::ostream& os);

/**
 * Creates the singleton FatalActionManager if not already created and
 * registers the specified actions to run on failure.
 */
void registerFatalActions(FatalAction::FatalActionPtrList safe_actions,
                          FatalAction::FatalActionPtrList unsafe_actions,
                          Thread::ThreadFactory& thread_factory);

/**
 * Tries to run all of the safe fatal actions. Only one thread will
 * be allowed to run the fatal functions. This returns true if the caller should
 * continue running the failure functions and have ran the safe actions.
 * Otherwise it returns false.
 */
bool runSafeActions();

/**
 * Tries to run all of the unsafe fatal actions. Call this only
 * after calling runSafeActions.
 *
 * Returns whether the thread was successfully able to run the
 * unsafe actions.
 */
bool runUnsafeActions();
} // namespace FatalErrorHandler
} // namespace Envoy
