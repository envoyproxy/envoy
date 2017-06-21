#pragma once

#include <chrono>
#include <memory>
#include <utility>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/server/configuration.h"
#include "envoy/server/guarddog.h"

#include "common/api/api_impl.h"
#include "common/common/thread.h"

#include "server/connection_handler_impl.h"

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
 *   1) ConnectionHandler/Dispatcher support:
 *      open connections, open files, callback posting, timers, listen
 *   2) GuardDog deadlock monitoring
 *
 * These features are not:
 *   1) Thread local storage (we don't want runOnAllThreads callbacks to run on
 *      this thread).
 *   2) Listeners in the ConnectionHandler.
 */
class DispatchedThreadImpl : Logger::Loggable<Envoy::Logger::Id::main> {
public:
  DispatchedThreadImpl()
      : handler_(new Envoy::Server::ConnectionHandlerImpl(
            log(), Api::ApiPtr{new Api::Impl(std::chrono::milliseconds(1000))})) {}

  /**
   * Start the thread.
   *
   * @param guard_dog GuardDog instance to register with.
   */
  void start(Envoy::Server::GuardDog& guard_dog);

  Event::Dispatcher& dispatcher() { return handler_->dispatcher(); }
  Network::ConnectionHandler& handler() { return *handler_; }

  /**
   * Exit the dispatched thread.  Will block until the thread joins.
   */
  void exit();

private:
  void threadRoutine(Envoy::Server::GuardDog& guard_dog);

  Envoy::Server::ConnectionHandlerImplPtr handler_;
  Thread::ThreadPtr thread_;
  Event::TimerPtr no_exit_timer_;
};

} // Event
} // Envoy
