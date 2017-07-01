#pragma once

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
   * Add a listener to the worker.
   * @param listener supplies the listener to add.
   */
  virtual void addListener(Listener& listener) PURE;

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
  virtual void removeListener(Listener& listener, std::function<void()> completion) PURE;

  /**
   * Stop a listener from accepting new connections. This is used for server draining.
   * @param listener supplies the listener to stop.
   */
  virtual void stopListener(Listener& listener) PURE;

  /**
   * Stop all listeners from accepting new connections. This is used for server draining.
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

} // Server
} // Envoy
