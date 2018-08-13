#pragma once

#include <unordered_map>

#include "envoy/common/pure.h"
#include "envoy/thread_local/thread_local.h"

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
 * Thread-local copy of the state of each configured overload action.
 */
class ThreadLocalOverloadState : public ThreadLocal::ThreadLocalObject {
public:
  const OverloadActionState& getState(const std::string& action) {
    auto it = actions_.find(action);
    if (it == actions_.end()) {
      it = actions_.insert(std::make_pair(action, OverloadActionState::Inactive)).first;
    }
    return it->second;
  }

  void setState(const std::string& action, OverloadActionState state) {
    auto it = actions_.find(action);
    if (it == actions_.end()) {
      actions_[action] = state;
    } else {
      it->second = state;
    }
  }

private:
  std::unordered_map<std::string, OverloadActionState> actions_;
};

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
   * Register a callback to be invoked when the specified overload action changes state
   * (ie. becomes activated or inactivated). Must be called before the start method is called.
   * @param action const std::string& the name of the overload action to register for
   * @param dispatcher Event::Dispatcher& the dispatcher on which callbacks will be posted
   * @param callback OverloadActionCb the callback to post when the overload action
   *        changes state
   */
  virtual void registerForAction(const std::string& action, Event::Dispatcher& dispatcher,
                                 OverloadActionCb callback) PURE;

  /**
   * Get the thread-local overload action states. Lookups in this object can be used as
   * an alternative to registering a callback for overload action state changes.
   */
  virtual ThreadLocalOverloadState& getThreadLocalOverloadState() PURE;
};

} // namespace Server
} // namespace Envoy
