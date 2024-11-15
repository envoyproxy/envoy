#pragma once

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"

#include "source/common/common/thread_synchronizer.h"

#include "library/common/types/c_types.h"

namespace Envoy {
namespace Event {

/**
 * Wrapper around Envoy's Event::Dispatcher that queues callbacks until drain() is called. Future
 * versions may support correct calling semantics after the Event::Dispatcher has been
 * terminated/deleted or before it has been created.
 */
class ProvisionalDispatcher : public ScopeTracker {
public:
  ProvisionalDispatcher() = default;
  virtual ~ProvisionalDispatcher() = default;

  // ScopeTracker
  void pushTrackedObject(const ScopeTrackedObject* object) override;
  void popTrackedObject(const ScopeTrackedObject* expected_object) override;
  bool trackedObjectStackIsEmpty() const override;

  /**
   * Drains all queued callbacks to the real dispatcher. Must be called after the underlying
   * dispatcher is running. Further posts will be transparently passed through.
   */
  virtual void drain(Event::Dispatcher& event_dispatcher);

  // TODO(goaway): return ENVOY_FAILURE after the underlying dispatcher has exited.
  /**
   * Before the Event::Dispatcher is running, queues posted callbacks; afterwards passes them
   * through.
   * @param callback, the callback to be dispatched.
   * @return should return ENVOY_FAILURE when the Event::Dispatcher exits, but at present it
   * always returns ENVOY_SUCCESS.
   */
  virtual envoy_status_t post(Event::PostCb callback);

  /**
   * Allocates a schedulable callback. @see SchedulableCallback for docs on how to use the wrapped
   * callback.
   * @param cb supplies the callback to invoke when the SchedulableCallback is triggered on the
   * event loop.
   * Must be called from context where ProvisionalDispatcher::isThreadSafe() is true.
   */
  virtual Event::SchedulableCallbackPtr createSchedulableCallback(std::function<void()> cb);

  /**
   * @return false before the Event::Dispatcher is running, otherwise the result of the
   * underlying call to Event::Dispatcher::isThreadSafe().
   */
  virtual bool isThreadSafe() const;

  /**
   * Submits an item for deferred delete. Must be called from context where
   * ProvisionalDispatcher::isThreadSafe() is true.
   */
  virtual void deferredDelete(DeferredDeletablePtr&& to_delete);

  /**
   * Exposes the TimeSource held by the underlying Event::Dispatcher.
   */
  virtual TimeSource& timeSource();

  /**
   * Marks the dispatcher as terminated, preventing any future work from being enqueued.
   */
  virtual void terminate();

  // Used for testing.
  Thread::ThreadSynchronizer& synchronizer() { return synchronizer_; }

private:
  // TODO(goaway): This class supports a straightforward case-specific lock-free implementation, but
  // uses heavyweight synchronization for expediency at present.
  Thread::MutexBasicLockable state_lock_;
  bool drained_ ABSL_GUARDED_BY(state_lock_){};
  std::list<Event::PostCb> init_queue_ ABSL_GUARDED_BY(state_lock_);
  Event::Dispatcher* event_dispatcher_{};
  Thread::ThreadSynchronizer synchronizer_;
  bool terminated_ ABSL_GUARDED_BY(state_lock_){};
};

using ProvisionalDispatcherPtr = std::unique_ptr<ProvisionalDispatcher>;

} // namespace Event
} // namespace Envoy
