#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"

namespace Envoy {
namespace ThreadLocal {

/**
 * All objects that are stored via the ThreadLocal interface must derive from this type.
 */
class ThreadLocalObject {
public:
  virtual ~ThreadLocalObject() {}

  /**
   * The owning thread is about to exit. Cleanup any variables that may reference other thread
   * locals.
   */
  virtual void shutdown() PURE;
};

typedef std::shared_ptr<ThreadLocalObject> ThreadLocalObjectSharedPtr;

/**
 * Interface for getting and setting thread local data as well as registering a thread
 */
class Instance {
public:
  virtual ~Instance() {}

  /**
   * Get a dedicated slot ID for use in further calls to get() and set().
   */
  virtual uint32_t allocateSlot() PURE;

  /**
   * Get a thread local index stored in the specified slot ID.
   */
  virtual ThreadLocalObjectSharedPtr get(uint32_t index) PURE;

  /**
   * This is a helper on top of get() that casts the object stored in the slot to the specified
   * type. No type information is specified explicitly in code so dynamic_cast provides some level
   * of protection via RTTI.
   */
  template <class T> T& getTyped(uint32_t index) {
    return *std::dynamic_pointer_cast<T>(get(index));
  }

  /**
   * A thread (via its dispatcher) must be registered before set() is called to receive thread
   * local data updates.
   * @param dispatcher supplies the thread's dispatcher.
   * @param main_thread supplies whether this is the main program thread or a worker thread.
   */
  virtual void registerThread(Event::Dispatcher& dispatcher, bool main_thread) PURE;

  /**
   * Run a callback on all registered threads.
   * @param cb supplies the callback to run.
   */
  virtual void runOnAllThreads(Event::PostCb cb) PURE;

  /**
   * Set thread local data on all threads previously registered via registerThread().
   * @param index species the slot ID which is used in subsequent calls to get() and getTyped().
   * @param initializeCb supplies the functor that will be called *on each thread*. The functor
   *                     returns the thread local object which is then stored. The storage is via
   *                     a shared_ptr. Thus, this is a flexible mechanism that can be used to share
   *                     the same data across all threads or to share different data on each thread.
   */
  typedef std::function<ThreadLocalObjectSharedPtr(Event::Dispatcher& dispatcher)> InitializeCb;
  virtual void set(uint32_t index, InitializeCb cb) PURE;

  /**
   * The owning thread is about to exit. Call shutdown() on all thread local objects.
   */
  virtual void shutdownThread() PURE;
};

} // ThreadLocal
} // Envoy
