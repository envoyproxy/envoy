#pragma once

#include <memory>

#include "envoy/network/drain_decision.h"

namespace Envoy {
namespace Server {

/**
 * Handles connection draining. An instance is generally shared across the entire server.
 */
class DrainManager : public Network::DrainDecision {
public:
  /**
   * @return TRUE if the manager is currently draining connections.
   */
  virtual bool draining() PURE;

  /**
   * Invoked in the secondary process to begin the drain procedure. (Making drain close operations
   * more likely).
   */
  virtual void startDrainSequence() PURE;

  /**
   * Invoked in the newly launched primary process to begin the parent shutdown sequence. At the end
   * of the sequence the previous primary process will be terminated.
   */
  virtual void startParentShutdownSequence() PURE;
};

typedef std::unique_ptr<DrainManager> DrainManagerPtr;

} // Server
} // Envoy
