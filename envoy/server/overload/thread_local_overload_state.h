#pragma once

#include <string>

#include "envoy/common/pure.h"
#include "envoy/event/scaled_range_timer_manager.h"
#include "envoy/event/timer.h"
#include "envoy/server/proactive_resource_monitor.h"
#include "envoy/thread_local/thread_local_object.h"

#include "source/common/common/interval_value.h"
#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Server {

enum class OverloadProactiveResourceName {
  GlobalDownstreamMaxConnections,
};

class OverloadProactiveResourceNameValues {
public:
  // Overload action to stop accepting new HTTP requests.
  const std::string GlobalDownstreamMaxConnections =
      "envoy.resource_monitors.global_downstream_max_connections";

  absl::flat_hash_map<std::string, OverloadProactiveResourceName>
      proactive_action_name_to_resource_ = {
          {GlobalDownstreamMaxConnections,
           OverloadProactiveResourceName::GlobalDownstreamMaxConnections}};
};

using OverloadProactiveResources = ConstSingleton<OverloadProactiveResourceNameValues>;

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
  /**
   * Invokes the corresponding resource monitor to allocate resource for given resource monitor in
   * a thread safe manner. Returns true if there is enough resource quota available and allocation
   * has succeeded, false if allocation failed or resource is not registered.
   * @param name of corresponding resource monitor.
   * @param increment to add to current resource usage value within monitor.
   */
  virtual bool tryAllocateResource(OverloadProactiveResourceName resource_name,
                                   int64_t increment) PURE;
  /**
   * Invokes the corresponding resource monitor to deallocate resource for given resource monitor in
   * a thread safe manner. Returns true if there is enough resource quota available and deallocation
   * has succeeded, false if deallocation failed or resource is not registered.
   * @param name of corresponding resource monitor.
   * @param decrement to subtract from current resource usage value within monitor.
   */
  virtual bool tryDeallocateResource(OverloadProactiveResourceName resource_name,
                                     int64_t decrement) PURE;

  /**
   * Checks if resource monitor is registered and resource usage tracking is
   * enabled in overload manager. Returns true if resource monitor is registered, false otherwise.
   * @param name of resource monitor to check.
   */
  virtual bool isResourceMonitorEnabled(OverloadProactiveResourceName resource_name) PURE;

  /**
   * Returns the proactive resource owned by the overload manager.
   * @param name of the proactive resource to retrieve.
   */
  virtual ProactiveResourceMonitorOptRef
  getProactiveResourceMonitorForTest(OverloadProactiveResourceName resource_name) PURE;
};

using ThreadLocalOverloadStateOptRef = OptRef<ThreadLocalOverloadState>;

} // namespace Server
} // namespace Envoy
