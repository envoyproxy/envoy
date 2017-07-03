#pragma once

#include <memory>

#include "envoy/api/api.h"
#include "envoy/network/connection_handler.h"
#include "envoy/server/guarddog.h"
#include "envoy/server/listener_manager.h"
#include "envoy/server/worker.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"
#include "common/common/thread.h"

namespace Envoy {
namespace Server {

class ProdWorkerFactory : public WorkerFactory, Logger::Loggable<Logger::Id::main> {
public:
  ProdWorkerFactory(ThreadLocal::Instance& tls, Api::Api& api) : tls_(tls), api_(api) {}

  // Server::WorkerFactory
  WorkerPtr createWorker() override;

private:
  ThreadLocal::Instance& tls_;
  Api::Api& api_;
};

/**
 * A server threaded worker that wraps up a worker thread, event loop, etc.
 */
class WorkerImpl : public Worker, Logger::Loggable<Logger::Id::main> {
public:
  WorkerImpl(ThreadLocal::Instance& tls, Event::DispatcherPtr&& dispatcher,
             Network::ConnectionHandlerPtr handler);

  // Server::Worker
  void addListener(Listener& listener) override;
  uint64_t numConnections() override;
  void removeListener(Listener& listener, std::function<void()> completion) override;
  void start(GuardDog& guard_dog) override;
  void stop() override;
  void stopListener(Listener& listener) override;
  void stopListeners() override;

private:
  void addListenerWorker(Listener& listener);
  void threadRoutine(GuardDog& guard_dog);

  ThreadLocal::Instance& tls_;
  Event::DispatcherPtr dispatcher_;
  Network::ConnectionHandlerPtr handler_;
  Thread::ThreadPtr thread_;
};

} // Server
} // Envoy
