#pragma once

#include <functional>
#include <memory>

#include "envoy/thread/thread.h"

namespace Envoy {
namespace Thread {

/**
 * A lock guard that deals with an optional lock and allows call-sites to release the lock prior to
 * the lock guard going out of scope.
 */
// TODO(junr03): this could be moved to Envoy's codebase.
class ABSL_SCOPED_LOCKABLE OptionalReleasableLockGuard {
public:
  /**
   * Establishes a scoped mutex-lock. If non-null, the mutex is locked upon construction.
   *
   * @param lock the mutex.
   */
  OptionalReleasableLockGuard(BasicLockable* lock) ABSL_EXCLUSIVE_LOCK_FUNCTION(lock)
      : lock_(lock) {
    if (lock_ != nullptr) {
      lock_->lock();
    }
  }

  /**
   * Destruction of the OptionalReleasableLockGuard unlocks the lock, if it is non-null and has not
   * already been explicitly released.
   */
  ~OptionalReleasableLockGuard() ABSL_UNLOCK_FUNCTION() { release(); }

  /**
   * Unlocks the mutex. This enables call-sites to release the mutex prior to the Lock going out of
   * scope.
   */
  void release() ABSL_UNLOCK_FUNCTION() {
    if (lock_ != nullptr) {
      lock_->unlock();
      lock_ = nullptr;
    }
  }

private:
  BasicLockable* lock_; // Set to nullptr on unlock, to prevent double-unlocking.
};

} // namespace Thread
} // namespace Envoy
