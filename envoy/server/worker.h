#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/common/random_generator.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/guarddog.h"
#include "envoy/server/overload/overload_manager.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Server {

/**
 * Interface for a threaded connection handling worker. All routines are thread safe.
 */
class Worker {
public:
  virtual ~Worker() = default;

  /**
   * Completion called when a listener has been added on a worker and is listening for new
   * connections.
   */
  using AddListenerCompletion = std::function<void()>;

  /**
   * Add a listener to the worker and replace the previous listener if any. If the previous listener
   * doesn't exist, the behavior should be equivalent to add a new listener.
   * @param overridden_listener The previous listener tag to be replaced. nullopt if it's a new
   * listener.
   * @param listener supplies the listener to add.
   * @param completion supplies the completion to call when the listener has been added (or not) on
   *                   the worker.
   * @param runtime, supplies the runtime for the server
   * @param random, supplies a random number generator
   */
  virtual void addListener(std::optional<uint64_t> overridden_listener,
                           Network::ListenerConfig& listener, AddListenerCompletion completion,
                           Runtime::Loader& runtime, Random::RandomGenerator& random) PURE;

  /**
   * @return uint64_t the number of connections across all listeners that the worker owns.
   */
  virtual uint64_t numConnections() const PURE;

  /**
   * Start the worker thread.
   * @param guard_dog supplies the optional guard dog to use for thread watching.
   * @param cb a callback to run when the worker thread starts running.
   * @param cpu_id an optional CPU to pin the worker thread to for CPU locality.
   */
  virtual void start(OptRef<GuardDog> guard_dog, const std::function<void()>& cb,
                     std::optional<uint32_t> cpu_id) PURE;

  /**
   * Initialize stats for this worker's dispatcher, if available. The worker will output
   * thread-specific stats under the given scope.
   * @param scope the scope to contain the new per-dispatcher stats created here.
   */
  virtual void initializeStats(Stats::Scope& scope) PURE;

  /**
   * Stop the worker thread.
   */
  virtual void stop() PURE;

  /**
   * Remove a listener from the worker.
   * @param listener supplies the listener to remove.
   * @param completion supplies the completion to be called when the listener has been removed.
   *        This completion is called on the worker thread. No locking is performed by the worker.
   */
  virtual void removeListener(Network::ListenerConfig& listener,
                              std::function<void()> completion) PURE;
  /**
   * Remove the stale filter chains of the given listener but leave the listener running.
   * @param listener_tag supplies the tag passed to addListener().
   * @param filter_chains supplies the filter chains to be removed.
   * @param completion supplies the completion to be called when the listener removed all the
   * untracked connections. This completion is called on the worker thread. No locking is performed
   * by the worker.
   */
  virtual void removeFilterChains(uint64_t listener_tag,
                                  const std::list<const Network::FilterChain*>& filter_chains,
                                  std::function<void()> completion) PURE;

  /**
   * Stop a listener from accepting new connections. This is used for server draining.
   * @param listener supplies the listener to stop.
   * @param options additional options to be passed through to shutdownListener.
   * @param completion supplies the completion to be called when the listener has stopped
   * accepting new connections. This completion is called on the worker thread. No locking is
   * performed by the worker.
   */
  virtual void stopListener(Network::ListenerConfig& listener,
                            const Network::ExtraShutdownListenerOptions& options,
                            std::function<void()> completion) PURE;

  /**
   * Notify all connections in the given filter chains of the listener that they are being
   * drained. This is intended to be invoked at the start of a drain sequence (before the
   * drain timer expires). Connections are not closed. This is a fire-and-forget operation
   * that is posted to the worker's dispatcher.
   * @param listener_tag supplies the tag passed to addListener().
   * @param filter_chains supplies the filter chains whose connections should be notified.
   */
  virtual void onFilterChainDrain(uint64_t listener_tag,
                                  const std::list<const Network::FilterChain*>& filter_chains) PURE;

  /**
   * Notify all connections of the given listener that they are being drained. Connections
   * are not closed. This is a fire-and-forget operation that is posted to the worker's
   * dispatcher.
   * @param listener supplies the listener whose connections should be notified.
   */
  virtual void onListenerDrain(Network::ListenerConfig& listener) PURE;
};

using WorkerPtr = std::unique_ptr<Worker>;

/**
 * Factory for creating workers.
 */
class WorkerFactory {
public:
  virtual ~WorkerFactory() = default;

  /**
   * @param index supplies the index of the worker, in the range of [0, concurrency).
   * @param overload_manager supplies the server's overload manager.
   * @param null_overload_manager supplies the server's null overload manager for conditions where
   * overload manager is disabled.
   * @param worker_name supplies the name of the worker, used for per-worker stats.
   * @return WorkerPtr a new worker.
   */
  virtual WorkerPtr createWorker(uint32_t index, OverloadManager& overload_manager,
                                 OverloadManager& null_overload_manager,
                                 const std::string& worker_name) PURE;
};

} // namespace Server
} // namespace Envoy
