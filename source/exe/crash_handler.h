#pragma once

#include "envoy/common/pure.h"

namespace Envoy {

// A simple class which allows registering functions to be called when Envoy
// receives one of the fatal signals, documented below.
//
// This is split out of signal_action.h because it is exempted from various
// builds.
class CrashHandlerInterface {
public:
  virtual ~CrashHandlerInterface() {}
  virtual void crashHandler() const PURE;
};

} // namespace Envoy
