#pragma once

#include <functional>
#include <memory>

#include "envoy/api/api.h"
#include "envoy/network/connection_handler.h"
#include "envoy/server/guarddog.h"
#include "envoy/server/listener_manager.h"
#include "envoy/server/worker.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/server/listener_hooks.h"

namespace Envoy {
namespace Server {

// Captures a set of stat names for the workers.
struct WorkerStatNames {
  explicit WorkerStatNames(Stats::SymbolTable& symbol_table)
      : pool_(symbol_table),
        reset_high_memory_stream_(pool_.add(OverloadActionStatsNames::get().ResetStreamsCount)) {}

  Stats::StatNamePool pool_;
  Stats::StatName reset_high_memory_stream_;
};

class ProdWorkerFactory : public WorkerFactory, Logger::Loggable<Logger::Id::main> {
public:
  ProdWorkerFactory(ThreadLocal::Instance& tls, Api::Api& api, ListenerHooks& hooks)
      : tls_(tls), api_(api), stat_names_(api.rootScope().symbolTable()), hooks_(hooks) {}

  // Server::WorkerFactory
  WorkerPtr createWorker(uint32_t index, OverloadManager& overload_manager,
                         OverloadManager& null_overload_manager,
                         const std::string& worker_name) override;

private:
  ThreadLocal::Instance& tls_;
  Api::Api& api_;
  WorkerStatNames stat_names_;
  ListenerHooks& hooks_;
};

/**
 * A server threaded worker that wraps up a worker thread, event loop, etc.
 */
class WorkerImpl : public Worker, Logger::Loggable<Logger::Id::main> {
public:
  WorkerImpl(ThreadLocal::Instance& tls, ListenerHooks& hooks, Event::DispatcherPtr&& dispatcher,
             Network::ConnectionHandlerPtr handler, OverloadManager& overload_manager,
             Api::Api& api, WorkerStatNames& stat_names);

  // Server::Worker
  void addListener(absl::optional<uint64_t> overridden_listener, Network::ListenerConfig& listener,
                   AddListenerCompletion completion, Runtime::Loader& loader,
                   Random::RandomGenerator& random) override;
  uint64_t numConnections() const override;

  void removeListener(Network::ListenerConfig& listener, std::function<void()> completion) override;
  void removeFilterChains(uint64_t listener_tag,
                          const std::list<const Network::FilterChain*>& filter_chains,
                          std::function<void()> completion) override;
  void start(OptRef<GuardDog> guard_dog, const std::function<void()>& cb) override;
  void initializeStats(Stats::Scope& scope) override;
  void stop() override;
  void stopListener(Network::ListenerConfig& listener,
                    const Network::ExtraShutdownListenerOptions& options,
                    std::function<void()> completion) override;

private:
  void threadRoutine(OptRef<GuardDog> guard_dog, const std::function<void()>& cb);
  void stopAcceptingConnectionsCb(OverloadActionState state);
  void rejectIncomingConnectionsCb(OverloadActionState state);
  void resetStreamsUsingExcessiveMemory(OverloadActionState state);

  ThreadLocal::Instance& tls_;
  ListenerHooks& hooks_;
  Event::DispatcherPtr dispatcher_;
  Network::ConnectionHandlerPtr handler_;
  Api::Api& api_;
  Stats::Counter& reset_streams_counter_;
  Thread::ThreadPtr thread_;
  WatchDogSharedPtr watch_dog_;
};

} // namespace Server
} // namespace Envoy
