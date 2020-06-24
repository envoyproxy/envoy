#pragma once

#include <string>
#include <unordered_map>

#include "envoy/common/pure.h"
#include "envoy/event/timer.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/macros.h"
#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Server {

/**
 * Tracks the state of an overload action. The state is a number between 0 and 1 that represents the
 * level of saturation. The values are categorized in three groups:
 * - Inactive (value = 0): indicates that an overload action is inactive because none of its
 *   triggers have fired.
 * - Scaling (0 < value < 1): indicates that an overload action is taking effect because at least
 *   one of its triggers is partially active.
 * - Saturated (value = 1): indicates that an overload action is active because at least one of its
 *   triggers has reached saturation.
 */
struct OverloadActionState {

  static OverloadActionState inactive() { return OverloadActionState(0); }

  static OverloadActionState saturated() { return OverloadActionState(1.0); }

  explicit constexpr OverloadActionState(double value)
      : action_value(value < 0 ? 0 : value > 1 ? 1 : value) {}

#define CMP_OPERATOR(OP)                                                                           \
  bool operator OP(const OverloadActionState& other) const {                                       \
    return action_value OP other.action_value;                                                     \
  }
  CMP_OPERATOR(==);
  CMP_OPERATOR(!=);
  CMP_OPERATOR(<);
  CMP_OPERATOR(<=);
  CMP_OPERATOR(>=);
  CMP_OPERATOR(>);
#undef CMP_OPERATOR

  double action_value;
};

/**
 * Callback invoked when an overload action changes state.
 */
using OverloadActionCb = std::function<void(OverloadActionState)>;

/**
 * Factory function exposed by the overload manager for creating timers.
 * @param class The type of timer being created. This can be used by overload actions that affect
 * all timers of the same class.
 * @param callback The callback to invoke when the timer is triggered.
 */
using OverloadTimerFactory = std::function<Event::TimerPtr(absl::string_view, Event::TimerCb)>;

/**
 * Thread-local copy of the state of each configured overload action.
 */
class ThreadLocalOverloadState {
public:
  virtual ~ThreadLocalOverloadState() = default;

  // Get a thread-local reference to the value for the given action key.
  virtual const OverloadActionState& getState(const std::string& action) PURE;

  // Sets the thread-local value for the given action key to the given state.
  virtual void setState(const std::string& action, OverloadActionState state) PURE;

  // Returns a factory for creating timers whose duration might be altered by
  // the overload manager.
  virtual OverloadTimerFactory getTimerFactory() PURE;
};

/**
 * Well-known overload action names.
 */
class OverloadActionNameValues {
public:
  // Overload action to stop accepting new HTTP requests.
  const std::string StopAcceptingRequests = "envoy.overload_actions.stop_accepting_requests";

  // Overload action to disable http keepalive (for HTTP1.x).
  const std::string DisableHttpKeepAlive = "envoy.overload_actions.disable_http_keepalive";

  // Overload action to stop accepting new connections.
  const std::string StopAcceptingConnections = "envoy.overload_actions.stop_accepting_connections";

  // Overload action to try to shrink the heap by releasing free memory.
  const std::string ShrinkHeap = "envoy.overload_actions.shrink_heap";
};

using OverloadActionNames = ConstSingleton<OverloadActionNameValues>;

/**
 * The OverloadManager protects the Envoy instance from being overwhelmed by client
 * requests. It monitors a set of resources and notifies registered listeners if
 * configured thresholds for those resources have been exceeded.
 */
class OverloadManager {
public:
  virtual ~OverloadManager() = default;

  /**
   * Start a recurring timer to monitor resources and notify listeners when overload actions
   * change state.
   */
  virtual void start() PURE;

  /**
   * Register a callback to be invoked when the specified overload action changes state
   * (i.e., becomes activated or inactivated). Must be called before the start method is called.
   * @param action const std::string& the name of the overload action to register for
   * @param dispatcher Event::Dispatcher& the dispatcher on which callbacks will be posted
   * @param callback OverloadActionCb the callback to post when the overload action
   *        changes state
   * @returns true if action was registered and false if no such action has been configured
   */
  virtual bool registerForAction(const std::string& action, Event::Dispatcher& dispatcher,
                                 OverloadActionCb callback) PURE;

  /**
   * Get the thread-local overload action states. Lookups in this object can be used as
   * an alternative to registering a callback for overload action state changes.
   */
  virtual ThreadLocalOverloadState& getThreadLocalOverloadState() PURE;
};

} // namespace Server
} // namespace Envoy
