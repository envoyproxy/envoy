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
};

typedef std::shared_ptr<ThreadLocalObject> ThreadLocalObjectSharedPtr;

/**
 * An individual allocated TLS slot. When the slot is destroyed the stored thread local will
 * be freed on each thread.
 */
class Slot {
public:
  virtual ~Slot() {}

  /**
   * @return ThreadLocalObjectSharedPtr a thread local object stored in the slot.
   */
  virtual ThreadLocalObjectSharedPtr get() PURE;

  /**
   * This is a helper on top of get() that casts the object stored in the slot to the specified
   * type. Since the slot only stores pointers to the base interface, dynamic_cast provides some
   * level of protection via RTTI.
   */
  template <class T> T& getTyped() { return *std::dynamic_pointer_cast<T>(get()); }

  /**
   * Run a callback on all registered threads.
   * @param cb supplies the callback to run.
   */
  virtual void runOnAllThreads(Event::PostCb cb) PURE;

  /**
   * Set thread local data on all threads previously registered via registerThread().
   * @param initializeCb supplies the functor that will be called *on each thread*. The functor
   *                     returns the thread local object which is then stored. The storage is via
   *                     a shared_ptr. Thus, this is a flexible mechanism that can be used to share
   *                     the same data across all threads or to share different data on each thread.
   */
  typedef std::function<ThreadLocalObjectSharedPtr(Event::Dispatcher& dispatcher)> InitializeCb;
  virtual void set(InitializeCb cb) PURE;
};

typedef std::unique_ptr<Slot> SlotPtr;

/**
 * Interface used to allocate thread local slots.
 */
class SlotAllocator {
public:
  virtual ~SlotAllocator() {}

  /**
   * @return SlotPtr a dedicated slot for use in further calls to get(), set(), etc.
   */
  virtual SlotPtr allocateSlot() PURE;
};

/**
 * Interface for getting and setting thread local data as well as registering a thread
 */
class Instance : public SlotAllocator {
public:
  /**
   * A thread (via its dispatcher) must be registered before set() is called on any allocated slots
   * to receive thread local data updates.
   * @param dispatcher supplies the thread's dispatcher.
   * @param main_thread supplies whether this is the main program thread or not. (The only
   *                    difference is that callbacks fire immediately on the main thread when posted
   *                    from the main thread).
   */
  virtual void registerThread(Event::Dispatcher& dispatcher, bool main_thread) PURE;

  /**
   * This should be called by the main thread before any worker threads start to exit. This will
   * block TLS removal during slot destruction, given that worker threads are about to call
   * shutdownThread(). This avoids having to implement de-registration of threads.
   */
  virtual void shutdownGlobalThreading() PURE;

  /**
   * The owning thread is about to exit. This will free all thread local variables. It must be
   * called on the thread that is shutting down.
   */
  virtual void shutdownThread() PURE;

  /**
   * @return Event::Dispatcher& the thread local dispatcher.
   */
  virtual Event::Dispatcher& dispatcher() PURE;
};

} // namespace ThreadLocal
} // namespace Envoy
