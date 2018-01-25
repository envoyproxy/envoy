#pragma once

#include <functional>
#include <memory>

#include "envoy/api/api.h"
#include "envoy/network/connection_handler.h"
#include "envoy/server/guarddog.h"
#include "envoy/server/listener_manager.h"
#include "envoy/server/worker.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"
#include "common/common/thread.h"

#include "server/test_hooks.h"

namespace Envoy {
namespace Server {

class ProdWorkerFactory : public WorkerFactory, Logger::Loggable<Logger::Id::main> {
public:
  ProdWorkerFactory(ThreadLocal::Instance& tls, Api::Api& api, TestHooks& hooks)
      : tls_(tls), api_(api), hooks_(hooks) {}

  // Server::WorkerFactory
  WorkerPtr createWorker() override;

private:
  ThreadLocal::Instance& tls_;
  Api::Api& api_;
  TestHooks& hooks_;
};

/**
 * A server threaded worker that wraps up a worker thread, event loop, etc.
 */
class WorkerImpl : public Worker, Logger::Loggable<Logger::Id::main> {
public:
  WorkerImpl(ThreadLocal::Instance& tls, TestHooks& hooks, Event::DispatcherPtr&& dispatcher,
             Network::ConnectionHandlerPtr handler);

  // Server::Worker
  void addListener(Network::ListenerConfig& listener, AddListenerCompletion completion) override;
  uint64_t numConnections() override;
  void removeListener(Network::ListenerConfig& listener, std::function<void()> completion) override;
  void start(GuardDog& guard_dog) override;
  void stop() override;
  void stopListener(Network::ListenerConfig& listener) override;
  void stopListeners() override;

private:
  void threadRoutine(GuardDog& guard_dog);

  ThreadLocal::Instance& tls_;
  TestHooks& hooks_;
  Event::DispatcherPtr dispatcher_;
  Network::ConnectionHandlerPtr handler_;
  Thread::ThreadPtr thread_;
};

} // namespace Server
} // namespace Envoy
