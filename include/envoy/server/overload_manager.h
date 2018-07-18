#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace Server {

/**
 * The OverloadManager protects the Envoy instance from being overwhelmed by client
 * requests. It monitors a set of resources and notifies registered listeners if
 * configured thresholds for those resources have been exceeded.
 */
class OverloadManager {
public:
  virtual ~OverloadManager() {}

  /**
   * Start a recurring timer to monitor resources and notify listeners when overload actions
   * change state.
   */
  virtual void start() PURE;

  /**
   * Register a callback to be invoked when the specified action changes state (ie. becomes
   * activated or inactivated). The callback is posted with the given dispatcher.
   * Must only be called during Envoy initialization when still in single-threaded mode.
   */
  virtual void registerForAction(const std::string& action, Event::Dispatcher& dispatcher,
                                 std::function<void(bool)> callback) PURE;
};

} // namespace Server
} // namespace Envoy
