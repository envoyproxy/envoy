#pragma once

#include <string>
#include <unordered_map>

#include "envoy/common/pure.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/macros.h"
#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Server {

enum class OverloadActionState {
  /**
   * Indicates that an overload action is active because at least one of its triggers has fired.
   */
  Active,
  /**
   * Indicates that an overload action is inactive because none of its triggers have fired.
   */
  Inactive
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
