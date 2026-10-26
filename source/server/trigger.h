#pragma once

#include <memory>

#include "envoy/server/overload/overload_manager.h"

namespace Envoy {
namespace Server {

/**
 * Trigger encapsulates translating resource pressure into the corresponding
 * OverloadActionState.
 */
class Trigger {
public:
  virtual ~Trigger() = default;

  // Updates the current value of the metric and returns whether the trigger has changed state.
  virtual bool updateValue(double value) = 0;

  // Evaluates the action state for the given metric value without modifying trigger state.
  virtual OverloadActionState evaluate(double value) const = 0;

  // Returns the action state for the trigger.
  virtual OverloadActionState actionState() const = 0;
};
using TriggerPtr = std::unique_ptr<Trigger>;

} // namespace Server
} // namespace Envoy
