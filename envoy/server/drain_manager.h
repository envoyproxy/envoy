#pragma once

#include <functional>
#include <memory>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/drain_decision.h"
#include "envoy/thread_local/thread_local_object.h"

namespace Envoy {
namespace Server {

class DrainManager;
using DrainManagerPtr = std::unique_ptr<DrainManager>;

/**
 * Handles connection draining. This concept is used globally during hot restart / server draining
 * as well as on individual listeners and filter-chains when they are being dynamically removed.
 */
class DrainManager : public Network::DrainDecision, public ThreadLocal::ThreadLocalObject {
public:
  /**
   * @brief Create a child drain-manager. Will proxy the drain status from the parent, but can also
   * be used to enact local draining.
   *
   * Child managers can be used to construct "drain trees" where each node in the tree can drain
   * independently of it's parent node, but the drain status cascades to child nodes.
   *
   * A notable difference to drain callbacks is that child managers are notified immediately and
   * without a delay timing. Additionally, notifications from parent to child is a thread-safe
   * operation whereas callback registration and triggering is not.
   *
   * @param dispatcher Dispatcher for the current thread in which the new child drain-manager will
   * exist.
   * @param drain_type The drain-type for the manager. May be different from the parent manager.
   */
  virtual DrainManagerPtr
  createChildManager(Event::Dispatcher& dispatcher,
                     envoy::config::listener::v3::Listener::DrainType drain_type) PURE;
  virtual DrainManagerPtr createChildManager(Event::Dispatcher& dispatcher) PURE;

  /**
   * Invoked to begin the drain procedure. (Making drain close operations more likely).
   * @param drain_complete_cb will be invoked once the drain sequence is finished. The parameter is
   * optional and can be an unassigned function.
   */
  virtual void startDrainSequence(std::function<void()> drain_complete_cb) PURE;

  /**
   * @return whether the drain sequence has started.
   */
  virtual bool draining() const PURE;

  /**
   * Invoked in the newly launched primary process to begin the parent shutdown sequence. At the end
   * of the sequence the previous primary process will be terminated.
   */
  virtual void startParentShutdownSequence() PURE;
};

using DrainManagerPtr = std::unique_ptr<DrainManager>;

} // namespace Server
} // namespace Envoy
