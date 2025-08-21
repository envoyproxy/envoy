#include "library/common/event/provisional_dispatcher.h"

#include "source/common/common/lock_guard.h"

namespace Envoy {
namespace Event {

void ProvisionalDispatcher::drain(Event::Dispatcher& event_dispatcher) {
  // TODO(goaway): Must be called from the Event::Dispatcher's thread, but we can't assert here
  // because of behavioral oddities in Event::Dispatcher: event_dispatcher_->isThreadSafe() will
  // crash.
  Thread::LockGuard lock(state_lock_);

  // Don't perform any work on the dispatcher if marked as terminated.
  if (terminated_) {
    event_dispatcher.exit();
    return;
  }

  RELEASE_ASSERT(!drained_, "ProvisionalDispatcher::drain must only occur once");
  drained_ = true;
  event_dispatcher_ = &event_dispatcher;

  while (!init_queue_.empty()) {
    event_dispatcher_->post(std::move(init_queue_.front()));
    init_queue_.pop_front();
  }
}

envoy_status_t ProvisionalDispatcher::post(Event::PostCb callback) {
  Thread::LockGuard lock(state_lock_);

  // Don't perform any work on the dispatcher if marked as terminated.
  if (terminated_) {
    return ENVOY_FAILURE;
  }

  if (drained_) {
    event_dispatcher_->post(std::move(callback));
    return ENVOY_SUCCESS;
  }

  init_queue_.push_back(std::move(callback));
  return ENVOY_SUCCESS;
}

Event::SchedulableCallbackPtr
ProvisionalDispatcher::createSchedulableCallback(std::function<void()> cb) {
  RELEASE_ASSERT(
      isThreadSafe(),
      "ProvisionalDispatcher::createSchedulableCallback must be called from a threadsafe context");
  return event_dispatcher_->createSchedulableCallback(cb);
}

bool ProvisionalDispatcher::isThreadSafe() const {
  // Doesn't require locking because if a thread has a stale view of drained_, then by definition
  // this wasn't a threadsafe call.
  return ABSL_TS_UNCHECKED_READ(drained_) && event_dispatcher_->isThreadSafe();
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

TimeSource& ProvisionalDispatcher::timeSource() { return event_dispatcher_->timeSource(); }

void ProvisionalDispatcher::terminate() {
  Thread::LockGuard lock(state_lock_);
  if (drained_) {
    event_dispatcher_->exit();
  }
  terminated_ = true;
}

} // namespace Event
} // namespace Envoy
