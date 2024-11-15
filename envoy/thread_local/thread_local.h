#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/thread_local/thread_local_object.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace ThreadLocal {

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
   * NOTE: The initialize callback is not supposed to capture the Slot, or its owner, as the owner
   * may be destructed in main thread before the update_cb gets called in a worker thread.
   */
  using InitializeCb = std::function<ThreadLocalObjectSharedPtr(Event::Dispatcher& dispatcher)>;
  virtual void set(InitializeCb cb) PURE;

protected:
  template <class T> friend class TypedSlot;

  /**
   * UpdateCb takes is passed a shared point to the current stored data. Use of
   * this API is deprecated; please use TypedSlot::runOnAllThreads instead.
   *
   * NOTE: The update callback is not supposed to capture the Slot, or its
   * owner, as the owner may be destructed in main thread before the update_cb
   * gets called in a worker thread.
   **/
  using UpdateCb = std::function<void(ThreadLocalObjectSharedPtr)>;

  // Callers must use the TypedSlot API, below.
  virtual void runOnAllThreads(const UpdateCb& update_cb) PURE;
  virtual void runOnAllThreads(const UpdateCb& update_cb,
                               const std::function<void()>& complete_cb) PURE;

  /**
   * Returns whether or not global threading has been shutdown.
   *
   * @return true if global threading has been shutdown or false if not.
   */
  virtual bool isShutdown() const PURE;
};

using SlotPtr = std::unique_ptr<Slot>;

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

// Provides a typesafe API for slots. The slot data must be derived from
// ThreadLocalObject. If there is no slot data, you can instantiated TypedSlot
// with the default type param: TypedSlot<> tls_;
//
// TODO(jmarantz): Rename the Slot class to something like RawSlot, where the
// only reference is from TypedSlot, which we can then rename to Slot.
template <class T> class TypedSlot {
public:
  /**
   * Helper method to create a unique_ptr for a typed slot. This helper
   * reduces some verbose parameterization at call-sites.
   *
   * @param allocator factory to allocate untyped Slot objects.
   * @return a TypedSlotPtr<T> (the type is defined below).
   */
  static std::unique_ptr<TypedSlot> makeUnique(SlotAllocator& allocator) {
    return std::make_unique<TypedSlot>(allocator);
  }

  explicit TypedSlot(SlotAllocator& allocator) : slot_(allocator.allocateSlot()) {}

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
   * NOTE: The initialize callback is not supposed to capture the Slot, or its owner, as the owner
   * may be destructed in main thread before the update_cb gets called in a worker thread.
   */
  using InitializeCb = std::function<std::shared_ptr<T>(Event::Dispatcher& dispatcher)>;
  void set(InitializeCb cb) { slot_->set(cb); }

  /**
   * @return an optional reference to the thread local object.
   */
  OptRef<T> get() { return getOpt(slot_->get()); }
  const OptRef<T> get() const { return getOpt(slot_->get()); }

  /**
   * Helper function to call methods on T. The caller is responsible
   * for ensuring that get().has_value() is true.
   *
   * @return a pointer to the thread local object.
   */
  T* operator->() { return &(slot_->getTyped<T>()); }
  const T* operator->() const { return &(slot_->getTyped<T>()); }

  /**
   * Helper function to get access to a T&. The caller is responsible for
   * ensuring that get().has_value() is true.
   *
   * @return a reference to the thread local object.
   */
  T& operator*() { return slot_->getTyped<T>(); }
  const T& operator*() const { return slot_->getTyped<T>(); }

  /**
   * UpdateCb is passed a mutable pointer to the current stored data. Callers
   * can assume that the passed-in OptRef has a value if they have called set(),
   * yielding a non-null shared_ptr, prior to runOnAllThreads().
   *
   * NOTE: The update callback is not supposed to capture the TypedSlot, or its
   * owner, as the owner may be destructed in main thread before the update_cb
   * gets called in a worker thread.
   */
  using UpdateCb = std::function<void(OptRef<T> obj)>;
  void runOnAllThreads(const UpdateCb& cb) { slot_->runOnAllThreads(makeSlotUpdateCb(cb)); }
  void runOnAllThreads(const UpdateCb& cb, const std::function<void()>& complete_cb) {
    slot_->runOnAllThreads(makeSlotUpdateCb(cb), complete_cb);
  }

  /**
   * Returns whether or not global threading has been shutdown.
   *
   * @return true if global threading has been shutdown or false if not.
   */
  bool isShutdown() const { return slot_->isShutdown(); };

private:
  static OptRef<T> getOpt(ThreadLocalObjectSharedPtr obj) {
    if (obj) {
      return OptRef<T>(obj->asType<T>());
    }
    return OptRef<T>();
  }

  Slot::UpdateCb makeSlotUpdateCb(UpdateCb cb) {
    return [cb](ThreadLocalObjectSharedPtr obj) { cb(getOpt(obj)); };
  }

  const SlotPtr slot_;
};

template <class T = ThreadLocalObject> using TypedSlotPtr = std::unique_ptr<TypedSlot<T>>;

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

  /**
   * Returns whether or not global threading has been shutdown.
   *
   * @return true if global threading has been shutdown or false if not.
   */
  virtual bool isShutdown() const PURE;
};

} // namespace ThreadLocal
} // namespace Envoy
