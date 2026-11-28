#pragma once

#include <cstdint>
#include <memory>

#include "envoy/event/dispatcher.h"

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

/**
 * The state that an object shares with the schedulers the module creates for it. A scheduler holds
 * this target instead of the object, so it never extends the lifetime of the object, and the
 * object is always destroyed by its own thread rather than by a module thread that commits an
 * event.
 *
 * The owner creates the target on its own thread when the module creates the first scheduler,
 * initializes it once the owner is held by a shared pointer and its dispatcher is known, and clears
 * it at the start of its teardown. Events committed before the target is initialized or after it
 * is cleared are dropped, and so are events that run after the owner is destroyed. Events that are
 * delivered call Owner::onScheduled(uint64_t) on the owner's thread.
 */
template <class Owner> class DynamicModuleSchedulerTarget {
public:
  /**
   * Starts posting committed events for the owner to the dispatcher.
   * @param dispatcher the dispatcher of the thread the owner runs on.
   * @param owner the owner of this target.
   */
  void initialize(Event::Dispatcher& dispatcher, std::weak_ptr<Owner> owner) {
    absl::MutexLock lock(mutex_);
    dispatcher_ = &dispatcher;
    owner_ = std::move(owner);
  }

  /**
   * Stops posting committed events. After this returns, no thread is posting to the dispatcher
   * through this target. The owner calls this before its teardown calls any module hook, since a
   * destroy hook may join a module thread that is waiting in commit().
   */
  void clear() {
    absl::MutexLock lock(mutex_);
    dispatcher_ = nullptr;
  }

  /**
   * Posts the event to the owner's dispatcher. Safe to call from any thread.
   * @param event_id the module defined ID of the event.
   */
  void commit(uint64_t event_id) {
    // The lock keeps the dispatcher alive across `post`, since the owner clears the pointer under
    // the same lock before the dispatcher can be destroyed.
    // TODO(wbpcode): find a lock-free way to do this, for example an atomic dispatcher pointer plus
    // a count of in-flight commits that the teardown waits on. An atomic pointer alone is not
    // enough: commit() could load a non-null pointer, then the owner and its dispatcher could be
    // destroyed before post() runs.
    absl::MutexLock lock(mutex_);
    if (dispatcher_ == nullptr) {
      return;
    }
    dispatcher_->post([owner = owner_, event_id]() {
      if (std::shared_ptr<Owner> owner_shared = owner.lock()) {
        owner_shared->onScheduled(event_id);
      }
    });
  }

private:
  absl::Mutex mutex_;
  Event::Dispatcher* dispatcher_ ABSL_GUARDED_BY(mutex_){nullptr};
  std::weak_ptr<Owner> owner_ ABSL_GUARDED_BY(mutex_);
};

template <class Owner>
using DynamicModuleSchedulerTargetSharedPtr = std::shared_ptr<DynamicModuleSchedulerTarget<Owner>>;

/**
 * Creates and initializes the scheduler target of an owner whose dispatcher is known from its
 * creation, unless it already exists. Owners create the target only when the module creates the
 * first scheduler, so an owner without schedulers neither allocates it nor locks its mutex. Must be
 * called on the owner's thread.
 * @param target the scheduler target member of the owner.
 * @param dispatcher the dispatcher of the thread the owner runs on.
 * @param owner the owner of the target.
 * @return the scheduler target.
 */
template <class Owner>
const DynamicModuleSchedulerTargetSharedPtr<Owner>&
ensureDynamicModuleSchedulerTarget(DynamicModuleSchedulerTargetSharedPtr<Owner>& target,
                                   Event::Dispatcher& dispatcher, std::weak_ptr<Owner> owner) {
  if (target == nullptr) {
    target = std::make_shared<DynamicModuleSchedulerTarget<Owner>>();
    target->initialize(dispatcher, std::move(owner));
  }
  return target;
}

/**
 * A scheduler that a module creates to run an event hook of an object on the object's thread from
 * any thread, including the ones the module manages.
 */
template <class Owner> class DynamicModuleScheduler {
public:
  explicit DynamicModuleScheduler(DynamicModuleSchedulerTargetSharedPtr<Owner> target)
      : target_(std::move(target)) {}

  /**
   * Schedules the event hook of the owner. Safe to call from any thread, and a no-op once the
   * owner has started its teardown.
   * @param event_id the module defined ID of the event.
   */
  void commit(uint64_t event_id) { target_->commit(event_id); }

private:
  const DynamicModuleSchedulerTargetSharedPtr<Owner> target_;
};

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
