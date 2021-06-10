#pragma once

#include <string>

#include "envoy/common/pure.h"
#include "envoy/event/scaled_range_timer_manager.h"
#include "envoy/event/timer.h"
#include "envoy/server/overload/proactive_resource_monitor.h"
#include "envoy/thread_local/thread_local_object.h"

#include "source/common/common/interval_value.h"

namespace Envoy {
namespace Server {

enum class OverloadProactiveResourceName {
  GlobalDownstreamMaxConnections,
};

class OverloadProactiveResourceNameValues {
public:
  // Overload action to stop accepting new HTTP requests.
  const std::string GlobalDownstreamMaxConnections =
      "envoy.proactive_resource_monitors.global_downstream_max_connections";

  std::set<std::string> proactive_resource_names_{GlobalDownstreamMaxConnections};

  absl::flat_hash_map<std::string, OverloadProactiveResourceName>
      proactive_action_name_to_resource_ = {
          {GlobalDownstreamMaxConnections,
           OverloadProactiveResourceName::GlobalDownstreamMaxConnections}};
};

using OverloadProactiveResourceNames = ConstSingleton<OverloadProactiveResourceNameValues>;

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

  UnitFloat value() const { return action_value_; }
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

  virtual bool tryAllocateResource(OverloadProactiveResourceName resource_name,
                                   int64_t increment) PURE;

  virtual bool tryDeallocateResource(OverloadProactiveResourceName resource_name,
                                     int64_t decrement) PURE;

  virtual int64_t currentResourceUsage(OverloadProactiveResourceName resource_name) PURE;
};

} // namespace Server
} // namespace Envoy
