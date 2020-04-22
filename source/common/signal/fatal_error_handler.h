#pragma once

#include "envoy/common/pure.h"

namespace Envoy {

// A simple class which allows registering functions to be called when Envoy
// receives one of the fatal signals, documented below.
//
// This is split out of signal_action.h because it is exempted from various
// builds.
class FatalErrorHandlerInterface {
public:
  virtual ~FatalErrorHandlerInterface() = default;
  virtual void onFatalError() const PURE;
};

} // namespace Envoy
