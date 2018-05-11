#pragma once

#include "envoy/common/pure.h"

#include "absl/base/thread_annotations.h"

namespace Envoy {
namespace Thread {

/**
 * Like the C++11 "basic lockable concept" but a pure virtual interface vs. a template, and
 * with thread annotations.
 */
class LOCKABLE BasicLockable {
public:
  virtual ~BasicLockable() {}

  virtual void lock() EXCLUSIVE_LOCK_FUNCTION() PURE;
  virtual bool tryLock() EXCLUSIVE_TRYLOCK_FUNCTION(true) PURE;
  virtual void unlock() UNLOCK_FUNCTION() PURE;
};

/**
 * A lock guard that deals with an optional lock.
 */
class SCOPED_LOCKABLE OptionalLockGuard {
public:
  /**
   * Establishes a scoped mutex-lock. If non-null, the mutex is locked upon construction.
   *
   * @param lock the mutex.
   */
  OptionalLockGuard(BasicLockable* lock) EXCLUSIVE_LOCK_FUNCTION(lock) : lock_(lock) {
    if (lock_ != nullptr) {
      lock_->lock();
    }
  }

  /**
   * Destruction of the OptionalLockGuard unlocks the lock, if it is non-null.
   */
  ~OptionalLockGuard() UNLOCK_FUNCTION() {
    if (lock_ != nullptr) {
      lock_->unlock();
    }
  }

private:
  BasicLockable* lock_;
};

/**
 * Like LockGuard, but uses a tryLock() on construction rather than a lock().
 */
class SCOPED_LOCKABLE TryLockGuard {
public:
  /**
   * Establishes a scoped mutex-lock; the a mutex lock is attempted via tryLock, so
   * an expected outcome is that the lock may fail. isLocked() must be called to
   * determine whether he lock was actually acquired.
   *
   * @param lock the mutex.
   */
  TryLockGuard(BasicLockable& lock) EXCLUSIVE_TRYLOCK_FUNCTION(true)
      : lock_(lock.tryLock() ? &lock : nullptr) {}

  /**
   * Destruction of the DeferredLockGuard unlocks the lock, if it was locked.
   */
  ~TryLockGuard() UNLOCK_FUNCTION() {
    if (isLocked()) {
      lock_->unlock();
    }
  }

  /**
   * @return bool whether the lock was successfully acquired.
   */
  bool isLocked() const { return lock_ != nullptr; }

private:
  BasicLockable* lock_;
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
class SCOPED_LOCKABLE LockGuard {
public:
  /**
   * Establishes a scoped mutex-lock; the mutex is locked upon construction.
   *
   * @param lock the mutex.
   */
  explicit LockGuard(BasicLockable& lock) EXCLUSIVE_LOCK_FUNCTION(lock) : lock_(lock) {
    lock_.lock();
  }

  /**
   * Destruction of the LockGuard unlocks the lock.
   */
  ~LockGuard() UNLOCK_FUNCTION() { lock_.unlock(); }

private:
  BasicLockable& lock_; // Set to nullptr on unlock, to prevent double-unlocking.
};

/**
 * Implements a LockGuard that is identical to absl::ReleasableMutexLock, but takes a
 * BasicLockable& to allow usages to be agnostic to cross-process mutexes vs. single-process
 * mutexes.
 */
class SCOPED_LOCKABLE ReleasableLockGuard {
public:
  /**
   * Establishes a scoped mutex-lock; the mutex is locked upon construction.
   *
   * @param lock the mutex.
   */
  explicit ReleasableLockGuard(BasicLockable& lock) EXCLUSIVE_LOCK_FUNCTION(lock) : lock_(&lock) {
    lock_->lock();
  }

  /**
   * Destruction of the LockGuard unlocks the lock, if it has not already been explicitly released.
   */
  ~ReleasableLockGuard() UNLOCK_FUNCTION() { release(); }

  /**
   * Unlocks the mutex. This enables call-sites to release the mutex prior to the Lock going out of
   * scope. This is called release() for consistency with absl::ReleasableMutexLock.
   */
  void release() UNLOCK_FUNCTION() {
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
