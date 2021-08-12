#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/thread/thread.h"

namespace Envoy {
namespace Server {

/**
 * WatchDog objects are an individual thread's interface with the deadlock
 * GuardDog. A shared pointer to a WatchDog is obtained from the GuardDog at
 * thread startup. After this point the "touch" method must be called
 * periodically to avoid triggering the deadlock detector.
 */
class WatchDog {
public:
  virtual ~WatchDog() = default;

  /**
   * Manually indicate that you are still alive by calling this.
   *
   * When the watchdog is registered with a dispatcher, the dispatcher will periodically call this
   * method to indicate the thread is still alive. It should be called directly by the application
   * code in cases where the watchdog is not registered with a dispatcher.
   */
  virtual void touch() PURE;
  virtual Thread::ThreadId threadId() const PURE;
};

using WatchDogSharedPtr = std::shared_ptr<WatchDog>;

} // namespace Server
} // namespace Envoy
