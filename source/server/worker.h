#pragma once

#include <chrono>
#include <memory>

#include "envoy/server/guarddog.h"
#include "envoy/server/listener_manager.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/thread.h"

#include "server/connection_handler_impl.h"

namespace Envoy {
namespace Server {

/**
 * A server threaded worker that wraps up a worker thread, event loop, etc.
 */
class Worker : Logger::Loggable<Logger::Id::main> {
public:
  Worker(ThreadLocal::Instance& tls, std::chrono::milliseconds file_flush_interval_msec);
  ~Worker();

  Event::Dispatcher& dispatcher() { return handler_->dispatcher(); }
  Network::ConnectionHandler* handler() { return handler_.get(); }
  void initializeConfiguration(ListenerManager& listener_manager, GuardDog& guard_dog);

  /**
   * Exit the worker. Will block until the worker thread joins. Called from the main thread.
   */
  void exit();

private:
  void onNoExitTimer();
  void threadRoutine(GuardDog& guard_dog);

  ThreadLocal::Instance& tls_;
  ConnectionHandlerImplPtr handler_;
  Thread::ThreadPtr thread_;
};

typedef std::unique_ptr<Worker> WorkerPtr;

} // Server
} // Envoy
