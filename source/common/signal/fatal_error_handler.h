#pragma once

#include <ostream>

#include "envoy/common/pure.h"

#include "source/common/signal/fatal_action.h"

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
 * be allowed to run the fatal functions.
 *
 * Returns a FatalAction::Status which the caller should use to
 * determine how to proceed.
 */
FatalAction::Status runSafeActions();

/**
 * Tries to run all of the unsafe fatal actions. Call this only
 * after calling runSafeActions.
 *
 * Returns a FatalAction::Status which the caller should use to determine
 * how to proceed.
 */
FatalAction::Status runUnsafeActions();

/**
 *  Clear the Fatal Actions at the end of the server's call to terminate().
 *  We should clean up the Fatal Action to prevent the memory from leaking.
 */
void clearFatalActionsOnTerminate();
} // namespace FatalErrorHandler
} // namespace Envoy
