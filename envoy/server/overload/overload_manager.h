#pragma once

#include <string>

#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/scaled_range_timer_manager.h"
#include "envoy/server/overload/load_shed_point.h"
#include "envoy/server/overload/thread_local_overload_state.h"

#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Server {
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

  // Overload action to reject (accept and then close) new connections.
  const std::string RejectIncomingConnections =
      "envoy.overload_actions.reject_incoming_connections";

  // Overload action to try to shrink the heap by releasing free memory.
  const std::string ShrinkHeap = "envoy.overload_actions.shrink_heap";

  // Overload action to reduce some subset of configured timeouts.
  const std::string ReduceTimeouts = "envoy.overload_actions.reduce_timeouts";

  // Overload action to reset streams using excessive memory.
  const std::string ResetStreams = "envoy.overload_actions.reset_high_memory_stream";

  // This should be kept current with the Overload actions available.
  // This is the last member of this class to duplicating the strings with
  // proper lifetime guarantees.
  const std::array<absl::string_view, 7> WellKnownActions = {StopAcceptingRequests,
                                                             DisableHttpKeepAlive,
                                                             StopAcceptingConnections,
                                                             RejectIncomingConnections,
                                                             ShrinkHeap,
                                                             ReduceTimeouts,
                                                             ResetStreams};
};

using OverloadActionNames = ConstSingleton<OverloadActionNameValues>;

/**
 * Well-known overload action stats.
 */
class OverloadActionStatsNameValues {
public:
  // Count of the number of streams the reset streams action has reset
  const std::string ResetStreamsCount = "envoy.overload_actions.reset_high_memory_stream.count";
};

using OverloadActionStatsNames = ConstSingleton<OverloadActionStatsNameValues>;

/**
 * The OverloadManager protects the Envoy instance from being overwhelmed by client
 * requests. It monitors a set of resources and notifies registered listeners if
 * configured thresholds for those resources have been exceeded.
 */
class OverloadManager : public LoadShedPointProvider {
public:
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

  /**
   * Get a factory for constructing scaled timer managers that respond to overload state.
   */
  virtual Event::ScaledRangeTimerManagerFactory scaledTimerFactory() PURE;

  /**
   * Stop the overload manager timer and wait for any pending resource updates to complete.
   * After this returns, overload manager clients should not receive any more callbacks
   * about overload state changes.
   */
  virtual void stop() PURE;
};

} // namespace Server
} // namespace Envoy
