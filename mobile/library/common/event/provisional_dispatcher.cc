#include "library/common/event/provisional_dispatcher.h"

#include "source/common/common/lock_guard.h"

#include "library/common/thread/lock_guard.h"

namespace Envoy {
namespace Event {

void ProvisionalDispatcher::drain(Event::Dispatcher& event_dispatcher) {
  // TODO(goaway): Must be called from the Event::Dispatcher's thread, but we can't assert here
  // because of behavioral oddities in Event::Dispatcher: event_dispatcher_->isThreadSafe() will
  // crash.
  Thread::LockGuard lock(state_lock_);
  RELEASE_ASSERT(!drained_, "ProvisionalDispatcher::drain must only occur once");
  drained_ = true;
  event_dispatcher_ = &event_dispatcher;

  for (const Event::PostCb& cb : init_queue_) {
    event_dispatcher_->post(cb);
  }
}

envoy_status_t ProvisionalDispatcher::post(Event::PostCb callback) {
  Thread::LockGuard lock(state_lock_);

  if (drained_) {
    event_dispatcher_->post(callback);
    return ENVOY_SUCCESS;
  }

  init_queue_.push_back(callback);
  return ENVOY_SUCCESS;
}

bool ProvisionalDispatcher::isThreadSafe() const {
  // Doesn't require locking because if a thread has a stale view of drained_, then by definition
  // this wasn't a threadsafe call.
  return TS_UNCHECKED_READ(drained_) && event_dispatcher_->isThreadSafe();
}

void ProvisionalDispatcher::deferredDelete(DeferredDeletablePtr&& to_delete) {
  RELEASE_ASSERT(isThreadSafe(),
                 "ProvisionalDispatcher::deferredDelete must be called from a threadsafe context");
  event_dispatcher_->deferredDelete(std::move(to_delete));
}

void ProvisionalDispatcher::pushTrackedObject(const ScopeTrackedObject* object) {
  RELEASE_ASSERT(
      isThreadSafe(),
      "ProvisionalDispatcher::pushTrackedObject must be called from a threadsafe context");
  event_dispatcher_->pushTrackedObject(object);
}

void ProvisionalDispatcher::popTrackedObject(const ScopeTrackedObject* expected_object) {
  RELEASE_ASSERT(
      isThreadSafe(),
      "ProvisionalDispatcher::popTrackedObject must be called from a threadsafe context");
  event_dispatcher_->popTrackedObject(expected_object);
}

bool ProvisionalDispatcher::trackedObjectStackIsEmpty() const {
  RELEASE_ASSERT(
      isThreadSafe(),
      "ProvisionalDispatcher::trackedObjectStackIsEmpty must be called from a threadsafe context");
  return event_dispatcher_->trackedObjectStackIsEmpty();
}

} // namespace Event
} // namespace Envoy
