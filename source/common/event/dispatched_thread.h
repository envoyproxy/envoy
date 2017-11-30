#pragma once

#include <chrono>
#include <memory>
#include <utility>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/server/configuration.h"
#include "envoy/server/guarddog.h"

#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"

namespace Envoy {
namespace Event {

/**
 * Generic dispatched thread.
 *
 * This provides basic functionality for a thread which has the "Dispatched
 * Nature" (runs an event loop) but does not want to do listening and accept new
 * connections like regular Worker threads, or any of the other functionality
 * specific to "Worker" threads. This is particularly useful if you need a
 * special purpose thread that will issue or receive gRPC calls.
 *
 * These features are set up:
 *   1) Dispatcher support:
 *      open connections, open files, callback posting, timers, listen
 *   2) GuardDog deadlock monitoring
 *
 * These features are not:
 *   1) Thread local storage (we don't want runOnAllThreads callbacks to run on
 *      this thread).
 *   2) ConnectionHandler and listeners
 *
 * TODO(dnoe): Worker should probably be refactored to leverage this.
 */
class DispatchedThreadImpl : Logger::Loggable<Envoy::Logger::Id::main> {
public:
  DispatchedThreadImpl() : dispatcher_(new DispatcherImpl()) {}

  /**
   * Start the thread.
   *
   * @param guard_dog GuardDog instance to register with.
   */
  void start(Envoy::Server::GuardDog& guard_dog);

  Dispatcher& dispatcher() { return *dispatcher_; }

  /**
   * Exit the dispatched thread. Will block until the thread joins.
   */
  void exit();

private:
  void threadRoutine(Envoy::Server::GuardDog& guard_dog);

  DispatcherPtr dispatcher_;
  Thread::ThreadPtr thread_;
};

} // namespace Event
} // namespace Envoy
