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

#include "server/listener_hooks.h"

namespace Envoy {
namespace Server {

class ProdWorkerFactory : public WorkerFactory, Logger::Loggable<Logger::Id::main> {
public:
  ProdWorkerFactory(ThreadLocal::Instance& tls, Api::Api& api, ListenerHooks& hooks)
      : tls_(tls), api_(api), hooks_(hooks) {}

  // Server::WorkerFactory
  WorkerPtr createWorker(OverloadManager& overload_manager,
                         const std::string& worker_name) override;

private:
  ThreadLocal::Instance& tls_;
  Api::Api& api_;
  ListenerHooks& hooks_;
};

/**
 * A server threaded worker that wraps up a worker thread, event loop, etc.
 */
class WorkerImpl : public Worker, Logger::Loggable<Logger::Id::main> {
public:
  WorkerImpl(ThreadLocal::Instance& tls, ListenerHooks& hooks, Event::DispatcherPtr&& dispatcher,
             Network::ConnectionHandlerPtr handler, OverloadManager& overload_manager,
             Api::Api& api);

  // Server::Worker
  void addListener(absl::optional<uint64_t> overridden_listener, Network::ListenerConfig& listener,
                   AddListenerCompletion completion) override;
  uint64_t numConnections() const override;

  void removeListener(Network::ListenerConfig& listener, std::function<void()> completion) override;
  void removeFilterChains(uint64_t listener_tag,
                          const std::list<const Network::FilterChain*>& filter_chains,
                          std::function<void()> completion) override;
  void start(GuardDog& guard_dog) override;
  void initializeStats(Stats::Scope& scope) override;
  void stop() override;
  void stopListener(Network::ListenerConfig& listener, std::function<void()> completion) override;

private:
  void threadRoutine(GuardDog& guard_dog);
  void stopAcceptingConnectionsCb(OverloadActionState state);

  ThreadLocal::Instance& tls_;
  ListenerHooks& hooks_;
  Event::DispatcherPtr dispatcher_;
  Network::ConnectionHandlerPtr handler_;
  Api::Api& api_;
  Thread::ThreadPtr thread_;
  WatchDogSharedPtr watch_dog_;
};

} // namespace Server
} // namespace Envoy
