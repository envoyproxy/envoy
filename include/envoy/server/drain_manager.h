#pragma once

#include <functional>
#include <memory>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/drain_decision.h"

namespace Envoy {
namespace Server {

class DrainManager;
using DrainManagerPtr = std::unique_ptr<DrainManager>;
using DrainManagerSharedPtr = std::shared_ptr<DrainManager>;

/**
 * Handles connection draining. This concept is used globally during hot restart / server draining
 * as well as on individual listeners and filter-chains when they are being dynamically removed.
 */
class DrainManager : public Network::DrainDecision {
public:
  /**
   * @brief Create a child drain-manager. Will proxy the drain status from the parent, but can also
   * be used to enact local draining.
   *
   * Child managers can be used to construct "drain trees" where each node in the tree can drain
   * independently of it's parent node, but the drain status cascades to child nodes. This
   * relationship is managed through the callback methods in the DrainDecision interface. When
   * constructed, the child is registered with the parent through this interface.
   *
   * The primary difference to a normal callback is that children managers do not respect the
   * drain-time parameter of the parent. This is because the children may have their own drain
   * strategies and may wish to define drain time and strategy separately. Observing this drain-time
   * at each layer may also lead to unexpected/unwanted delays.
   *
   * @param dispatcher Dispatcher for the current thread in which the new child drain-manager will
   * exist.
   * @param drain_type The drain-type for the manager. May be different from the parent manager.
   */
  virtual DrainManagerSharedPtr
  createChildManager(Event::Dispatcher& dispatcher,
                     envoy::config::listener::v3::Listener::DrainType drain_type) PURE;
  virtual DrainManagerSharedPtr createChildManager(Event::Dispatcher& dispatcher) PURE;

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

} // namespace Server
} // namespace Envoy
