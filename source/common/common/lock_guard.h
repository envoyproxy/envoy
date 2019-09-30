#pragma once

#include <functional>
#include <memory>

#include "envoy/thread/thread.h"

namespace Envoy {
namespace Thread {

/**
 * A lock guard that deals with an optional lock.
 */
class ABSL_SCOPED_LOCKABLE OptionalLockGuard {
public:
  /**
   * Establishes a scoped mutex-lock. If non-null, the mutex is locked upon construction.
   *
   * @param lock the mutex.
   */
  OptionalLockGuard(BasicLockable* lock) ABSL_EXCLUSIVE_LOCK_FUNCTION(lock) : lock_(lock) {
    if (lock_ != nullptr) {
      lock_->lock();
    }
  }

  /**
   * Destruction of the OptionalLockGuard unlocks the lock, if it is non-null.
   */
  ~OptionalLockGuard() ABSL_UNLOCK_FUNCTION() {
    if (lock_ != nullptr) {
      lock_->unlock();
    }
  }

private:
  BasicLockable* const lock_;
};

// At the moment, TryLockGuard is very hard to annotate correctly, I
// believe due to limitations in clang. At the moment there are no
// GUARDED_BY variables for any tryLocks in the codebase, so it's
// easiest just to leave it out. In a future clang release it's
// possible we can enable this. See also the commented-out block
// in ThreadTest.TestTryLockGuard in test/common/common/thread_test.cc.
#define DISABLE_TRYLOCKGUARD_ANNOTATION(annotation)

/**
 * Like LockGuard, but uses a tryLock() on construction rather than a lock(). This
 * class lacks thread annotations, as clang currently does appear to be able to handle
 * conditional thread annotations. So the ones we'd like are commented out.
 */
class ABSL_SCOPED_LOCKABLE TryLockGuard {
public:
  /**
   * Establishes a scoped mutex-lock; the a mutex lock is attempted via tryLock, so
   * an expected outcome is that the lock may fail. isLocked() must be called to
   * determine whether he lock was actually acquired.
   *
   * @param lock the mutex.
   */
  TryLockGuard(BasicLockable& lock) : lock_(lock) {}

  /**
   * Destruction of the TryLockGuard unlocks the lock, if it was locked.
   */
  ~TryLockGuard() DISABLE_TRYLOCKGUARD_ANNOTATION(UNLOCK_FUNCTION()) {
    if (is_locked_) {
      lock_.unlock();
    }
  }

  /**
   * @return bool whether the lock was successfully acquired.
   */
  bool tryLock() DISABLE_TRYLOCKGUARD_ANNOTATION(EXCLUSIVE_TRYLOCK_FUNCTION(true)) {
    is_locked_ = lock_.tryLock();
    return is_locked_;
  }

private:
  BasicLockable& lock_;
  bool is_locked_{false};
};

/**
 * Implements a LockGuard that is identical to absl::ReleasableMutexLock, but takes a
 * BasicLockable& to allow usages to be agnostic to cross-process mutexes vs. single-process
 * mutexes.
 *
 * Note: this implementation holds the mutex for the lifetime of the LockGuard, simplifying
 * implementation (no conditionals) and readability at call-sites. In some cases, an early
 * release is needed, in which case, a ReleasableLockGuard can be used.
 */
class ABSL_SCOPED_LOCKABLE LockGuard {
public:
  /**
   * Establishes a scoped mutex-lock; the mutex is locked upon construction.
   *
   * @param lock the mutex.
   */
  explicit LockGuard(BasicLockable& lock) ABSL_EXCLUSIVE_LOCK_FUNCTION(lock) : lock_(lock) {
    lock_.lock();
  }

  /**
   * Destruction of the LockGuard unlocks the lock.
   */
  ~LockGuard() ABSL_UNLOCK_FUNCTION() { lock_.unlock(); }

private:
  BasicLockable& lock_;
};

/**
 * Implements a LockGuard that is identical to absl::ReleasableMutexLock, but takes a
 * BasicLockable& to allow usages to be agnostic to cross-process mutexes vs. single-process
 * mutexes.
 */
class ABSL_SCOPED_LOCKABLE ReleasableLockGuard {
public:
  /**
   * Establishes a scoped mutex-lock; the mutex is locked upon construction.
   *
   * @param lock the mutex.
   */
  explicit ReleasableLockGuard(BasicLockable& lock) ABSL_EXCLUSIVE_LOCK_FUNCTION(lock)
      : lock_(&lock) {
    lock_->lock();
  }

  /**
   * Destruction of the LockGuard unlocks the lock, if it has not already been explicitly released.
   */
  ~ReleasableLockGuard() ABSL_UNLOCK_FUNCTION() { release(); }

  /**
   * Unlocks the mutex. This enables call-sites to release the mutex prior to the Lock going out of
   * scope. This is called release() for consistency with absl::ReleasableMutexLock.
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
