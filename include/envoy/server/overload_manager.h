#pragma once

#include "envoy/common/pure.h"

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
typedef std::function<void(OverloadActionState)> OverloadActionCb;

/**
 * The OverloadManager protects the Envoy instance from being overwhelmed by client
 * requests. It monitors a set of resources and notifies registered listeners if
 * configured thresholds for those resources have been exceeded.
 */
class OverloadManager {
public:
  virtual ~OverloadManager() {}

  /**
   * Register a callback to be invoked when the specified overload action changes state
   * (ie. becomes activated or inactivated). Must be called before the start method is called.
   * @param action const std::string& the name of the overload action to register for
   * @param dispatcher Event::Dispatcher& the dispatcher on which callbacks will be posted
   * @param callback OverloadActionCb the callback to post when the overload action
   *        changes state
   */
  virtual void registerForAction(const std::string& action, Event::Dispatcher& dispatcher,
                                 OverloadActionCb callback) PURE;
};

} // namespace Server
} // namespace Envoy
