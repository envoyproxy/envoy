#pragma once

#include "envoy/server/guarddog.h"

namespace Envoy {
namespace Server {

/**
 * Interface for a threaded connection handling worker.
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
