#pragma once

#include <chrono>
#include <memory>

#include "envoy/server/guarddog.h"
#include "envoy/server/listener_manager.h"
#include "envoy/server/options.h"
#include "envoy/server/worker.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/thread.h"

#include "server/connection_handler_impl.h"

namespace Envoy {
namespace Server {

class ProdWorkerFactory : public WorkerFactory {
public:
  ProdWorkerFactory(ThreadLocal::Instance& tls, Options& options) : tls_(tls), options_(options) {}

  // Server::WorkerFactory
  WorkerPtr createWorker() override;

private:
  ThreadLocal::Instance& tls_;
  Options& options_;
};

/**
 * A server threaded worker that wraps up a worker thread, event loop, etc.
 */
class WorkerImpl : public Worker, Logger::Loggable<Logger::Id::main> {
public:
  WorkerImpl(ThreadLocal::Instance& tls, std::chrono::milliseconds file_flush_interval_msec);

  // Server::Worker
  void addListener(Listener& listener) override;
  uint64_t numConnections() override;
  void start(GuardDog& guard_dog) override;
  void stop() override;
  void stopListeners() override;

private:
  void threadRoutine(GuardDog& guard_dog);

  ThreadLocal::Instance& tls_;
  ConnectionHandlerImplPtr handler_;
  Thread::ThreadPtr thread_;
};

} // Server
} // Envoy
