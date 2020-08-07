#pragma once

#include <ostream>

#include "envoy/common/pure.h"

namespace Envoy {

// A simple class which allows registering functions to be called when Envoy
// receives one of the fatal signals, documented in signal_action.h.
class FatalErrorHandlerInterface {
public:
  virtual ~FatalErrorHandlerInterface() = default;
  // Called when Envoy receives a fatal signal. Must be async-signal-safe: in
  // particular, it can't allocate memory.
  virtual void onFatalError(std::ostream& os) const PURE;
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
} // namespace FatalErrorHandler
} // namespace Envoy
