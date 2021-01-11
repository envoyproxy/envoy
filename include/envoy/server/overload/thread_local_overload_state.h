
#pragma once

#include <string>

#include "envoy/common/pure.h"
#include "envoy/event/scaled_range_timer_manager.h"
#include "envoy/event/scaled_timer_minimum.h"
#include "envoy/event/timer.h"
#include "envoy/thread_local/thread_local_object.h"

#include "common/common/interval_value.h"

namespace Envoy {
namespace Server {

/**
 * Tracks the state of an overload action. The state is a number between 0 and 1 that represents the
 * level of saturation. The values are categorized in two groups:
 * - Saturated (value = 1): indicates that an overload action is active because at least one of its
 *   triggers has reached saturation.
 * - Scaling (0 <= value < 1): indicates that an overload action is not saturated.
 */
class OverloadActionState {
public:
  static constexpr OverloadActionState inactive() { return OverloadActionState(UnitFloat::min()); }

  static constexpr OverloadActionState saturated() { return OverloadActionState(UnitFloat::max()); }

  explicit constexpr OverloadActionState(UnitFloat value) : action_value_(value) {}

  float value() const { return action_value_.value(); }
  bool isSaturated() const { return action_value_.value() == UnitFloat::max().value(); }

private:
  UnitFloat action_value_;
};

/**
 * Callback invoked when an overload action changes state.
 */
using OverloadActionCb = std::function<void(OverloadActionState)>;

/**
 * Thread-local copy of the state of each configured overload action.
 */
class ThreadLocalOverloadState : public ThreadLocal::ThreadLocalObject {
public:
  // Get a thread-local reference to the value for the given action key.
  virtual const OverloadActionState& getState(const std::string& action) PURE;

  // Get a scaled timer whose minimum corresponds to the configured value for the given timer type.
  virtual Event::TimerPtr createScaledTimer(Event::ScaledRangeTimerManager::TimerType timer_type,
                                            Event::TimerCb callback) PURE;

  // Get a scaled timer whose minimum is determined by the given scaling rule.
  virtual Event::TimerPtr createScaledTimer(Event::ScaledTimerMinimum minimum,
                                            Event::TimerCb callback) PURE;
};

} // namespace Server
} // namespace Envoy
