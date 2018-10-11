#pragma once

#include <functional>

#include "envoy/server/guarddog.h"

namespace Envoy {
namespace Server {

/**
 * Interface for a threaded connection handling worker. All routines are thread safe.
 */
class Worker {
public:
  virtual ~Worker() {}

  /**
   * Completion called when a listener has been added on a worker and is listening for new
   * connections.
   * @param success supplies whether the addition was successful or not. FALSE can be returned
   *                when there is a race condition between bind() and listen().
   */
  typedef std::function<void(bool success)> AddListenerCompletion;

  /**
   * Add a listener to the worker.
   * @param listener supplies the listener to add.
   * @param completion supplies the completion to call when the listener has been added (or not) on
   *                   the worker.
   */
  virtual void addListener(Network::ListenerConfig& listener,
                           AddListenerCompletion completion) PURE;

  /**
   * @return uint64_t the number of connections across all listeners that the worker owns.
   */
  virtual uint64_t numConnections() PURE;

  /**
   * Start the worker thread.
   * @param guard_dog supplies the guard dog to use for thread watching.
   */
  virtual void start(GuardDog& guard_dog) PURE;

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
   * Stop a listener from accepting new connections. This is used for server draining.
   * @param listener supplies the listener to stop.
   * TODO(mattklein123): We might consider adding a completion here in the future to tell us when
   * all connections are gone. This would allow us to remove the listener more quickly depending on
   * drain speed.
   */
  virtual void stopListener(Network::ListenerConfig& listener) PURE;

  /**
   * Stop all listeners from accepting new connections. This is used for server draining.
   * TODO(mattklein123): Same comment about the addition of a completion as stopListener().
   */
  virtual void stopListeners() PURE;
};

typedef std::unique_ptr<Worker> WorkerPtr;

/**
 * Factory for creating workers.
 */
class WorkerFactory {
public:
  virtual ~WorkerFactory() {}

  /**
   * @return WorkerPtr a new worker.
   */
  virtual WorkerPtr createWorker() PURE;
};

} // namespace Server
} // namespace Envoy
