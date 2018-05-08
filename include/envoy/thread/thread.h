u#pragma once

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
    if (lock_) {
      lock_->lock();
    }
  }

  /**
   * Destruction of the OptionalLockGuard unlocks the lock, if it is non-null.
   */
  ~OptionalLockGuard() UNLOCK_FUNCTION() {
    if (lock_) {
      lock_->unlock();
    }
  }

private:
  BasicLockable* lock_;
};

/**
 * A lock guard that deals with a lock that is not locked on construction, although
 * it is unlocked on destruction, if necessary.
 */
class DeferredLockGuard {
public:
  /**
   * Establishes a scoped mutex-lock; the mutex is not locked upon construction.
   *
   * @param lock the mutex.
   */
  DeferredLockGuard(BasicLockable& lock) : lock_(lock), is_locked_(false) {}

  /**
   * Destruction of the DeferredLockGuard unlocks the lock, if it was locked.
   */
  ~DeferredLockGuard() {
    if (is_locked_) {
      lock_.unlock();
    }
  }

  // Attempts to lock the mutex, if present. Returns false if no lock was taken.
  bool tryLock() {
    is_locked_ = lock_.tryLock();
    return is_locked_;
  }

private:
  BasicLockable& lock_;
  bool is_locked_;
};

class SCOPED_LOCKABLE LockGuard {
public:
  /**
   * Establishes a scoped mutex-lock; the mutex is locked upon construction.
   *
   * @param lock the mutex.
   */
  explicit LockGuard(BasicLockable& lock) EXCLUSIVE_LOCK_FUNCTION(lock) : lock_(&lock) {
    lock_->lock();
  }

  /**
   * Destruction of the LockGuard unlocks the lock, if it has not already been explicitly unlocked.
   */
  ~LockGuard() UNLOCK_FUNCTION() { unlock(); }

  /**
   * Unlocks the mutex. This enables call-sites to release the mutex prior to the Lock going out of
   * scope.
   */
  void unlock() UNLOCK_FUNCTION() {
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
