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
  virtual ~ThreadLocalObject() = default;

  /**
   * Return the object casted to a concrete type. See getTyped() below for comments on the casts.
   */
  template <class T> T& asType() {
    ASSERT(dynamic_cast<T*>(this) != nullptr);
    return *static_cast<T*>(this);
  }
};

using ThreadLocalObjectSharedPtr = std::shared_ptr<ThreadLocalObject>;

/**
 * An individual allocated TLS slot. When the slot is destroyed the stored thread local will
 * be freed on each thread.
 */
class Slot {
public:
  virtual ~Slot() = default;

  /**
   * Returns if there is thread local data for this thread.
   *
   * This should return true for Envoy worker threads and false for threads which do not have thread
   * local storage allocated.
   *
   * @return true if registerThread has been called for this thread, false otherwise.
   */
  virtual bool currentThreadRegistered() PURE;

  /**
   * @return ThreadLocalObjectSharedPtr a thread local object stored in the slot.
   */
  virtual ThreadLocalObjectSharedPtr get() PURE;

  /**
   * This is a helper on top of get() that casts the object stored in the slot to the specified
   * type. Since the slot only stores pointers to the base interface, the static_cast operates
   * in production for performance, and the dynamic_cast validates correctness in tests and debug
   * builds.
   */
  template <class T> T& getTyped() {
    ASSERT(std::dynamic_pointer_cast<T>(get()) != nullptr);
    return *static_cast<T*>(get().get());
  }

  /**
   * Set thread local data on all threads previously registered via registerThread().
   * @param initializeCb supplies the functor that will be called *on each thread*. The functor
   *                     returns the thread local object which is then stored. The storage is via
   *                     a shared_ptr. Thus, this is a flexible mechanism that can be used to share
   *                     the same data across all threads or to share different data on each thread.
   *
   * NOTE: The initialize callback is not supposed to capture the Slot, or its owner. As the owner
   * may be destructed in main thread before the update_cb gets called in a worker thread.
   */
  using InitializeCb = std::function<ThreadLocalObjectSharedPtr(Event::Dispatcher& dispatcher)>;
  virtual void set(InitializeCb cb) PURE;

  /**
   * UpdateCb takes the current stored data, and returns an updated/new version data.
   * TLS will run the callback and replace the stored data with the returned value *in each thread*.
   *
   * NOTE: The update callback is not supposed to capture the Slot, or its owner. As the owner may
   * be destructed in main thread before the update_cb gets called in a worker thread.
   **/
  using UpdateCb = std::function<ThreadLocalObjectSharedPtr(ThreadLocalObjectSharedPtr)>;
  virtual void runOnAllThreads(const UpdateCb& update_cb) PURE;
  virtual void runOnAllThreads(const UpdateCb& update_cb, Event::PostCb complete_cb) PURE;
};

using SlotPtr = std::unique_ptr<Slot>;

template<class T> class TypedSlot {
 public:
  /**
   * @return true if the TypedSlot object has been allocated a slot.
   */
  bool hasSlot() const { return slot_ != nullptr; }

  /**
   * @param slot The allocated slot object. Ownership transferred into the TypedSlot.
   */
  void setSlot(SlotPtr&& slot) { slot_ = std::move(slot); }

  /**
   * Returns if there is thread local data for this thread.
   *
   * This should return true for Envoy worker threads and false for threads which do not have thread
   * local storage allocated.
   *
   * @return true if registerThread has been called for this thread, false otherwise.
   */
  bool currentThreadRegistered() { return slot_->currentThreadRegistered(); }

  /**
   * Set thread local data on all threads previously registered via registerThread().
   * @param initializeCb supplies the functor that will be called *on each thread*. The functor
   *                     returns the thread local object which is then stored. The storage is via
   *                     a shared_ptr. Thus, this is a flexible mechanism that can be used to share
   *                     the same data across all threads or to share different data on each thread.
   *
   * NOTE: The initialize callback is not supposed to capture the Slot, or its owner. As the owner
   * may be destructed in main thread before the update_cb gets called in a worker thread.
   */
  using SharedT = std::shared_ptr<T>;
  using InitializeCb = std::function<SharedT(Event::Dispatcher& dispatcher)>;
  void set(InitializeCb cb) { slot_->set(cb); }

  T& get() { return slot_->getTyped<T>(); }

  /**
   * UpdateCb is passed a mutable reference to the current stored data.
   *
   * NOTE: The update callback is not supposed to capture the TypedSlot, or its owner. As the owner may
   * be destructed in main thread before the update_cb gets called in a worker thread.
   */
  using UpdateCb = std::function<void(T& obj)>;
  void runOnAllThreads(const UpdateCb& cb) { slot_->runOnAllThreads(makeSlotUpdateCb(cb)); }
  void runOnAllThreads(const UpdateCb& cb, Event::PostCb complete_cb) {
    slot_->runOnAllThreads(makeSlotUpdateCb(cb), complete_cb);
  }

 private:
  Slot::UpdateCb makeSlotUpdateCb(UpdateCb cb) {
    return [this, cb](ThreadLocalObjectSharedPtr obj) -> ThreadLocalObjectSharedPtr {
      T& typed_obj = slot_->getTyped<T>();
      cb(typed_obj);
      // Note: Better have a test for mutating the object.
      return obj;
    };
  }

  SlotPtr slot_;
};

/**
 * Interface used to allocate thread local slots.
 */
class SlotAllocator {
public:
  virtual ~SlotAllocator() = default;

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
