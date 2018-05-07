#pragma once

#include <functional>
#include <memory>
#include <mutex>

#include "envoy/thread/thread.h"

#include "common/common/non_copyable.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Thread {

typedef int32_t ThreadId;

/**
 * Wrapper for a pthread thread. We don't use std::thread because it eats exceptions and leads to
 * unusable stack traces.
 */
class Thread {
public:
  Thread(std::function<void()> thread_routine);

  /**
   * Get current thread id.
   */
  static ThreadId currentThreadId();

  /**
   * Join on thread exit.
   */
  void join();

private:
  std::function<void()> thread_routine_;
  pthread_t thread_id_;
};

typedef std::unique_ptr<Thread> ThreadPtr;

/**
 * Implementation of BasicLockable
 */
class MutexBasicLockable : public BasicLockable {
public:
  void lock() override { mutex_.lock(); }
  bool try_lock() override { return mutex_.try_lock(); }
  void unlock() override { mutex_.unlock(); }

private:
  // TODO(jmarantz): change to absl::Mutex and add thread annotations.
  std::mutex mutex_;
};

// Substitute for absl::MutexLock for use in Envoy, with non-const ref
// style, and more importantly, making it easier to release locks in
// the middle of scopes. absl::Mutex on its own is risky to use in
// the context of code that throws exceptions, as you don't want a
// thrown exception to leave something locked. However
// absl::MutexLock is hard to use when you need to interleave
// variable-scopes and lock-scopes, because it has no Unlock().
//
// Functionally, you could accomplish this with
// std::unique_ptr<absl::MutexLock>, but the clang lock analysis
// does not understand that composition, and would give false locking
// errors; we need a MutexLock with the functionality needed in the
// Envoy codebase (slightly beyond absl's) *and* the lock annotations.
//
// If https://github.com/abseil/abseil-cpp/issues/119 is resolved, this
// class can be eliminated.
class SCOPED_LOCKABLE MutexLock : public NonCopyable {
public:
  /**
   * Establishes a scoped mutex-lock; the mutex is locked upon construction.
   *
   * @param mutex the mutex.
   */
  explicit MutexLock(absl::Mutex& mutex) EXCLUSIVE_LOCK_FUNCTION(mutex) : mutex_(&mutex) {
    mutex_->Lock();
  }

  /**
   * Destruction of the MutexLock unlocks the mutex, if it has not already been explicitly unlocked.
   */
  ~MutexLock() UNLOCK_FUNCTION() { unlock(); }

  /**
   * Unlocks the mutex. This enables call-sites to release the mutex prior to the Lock going out of
   * scope.
   */
  void unlock() UNLOCK_FUNCTION() {
    if (mutex_ != nullptr) {
      mutex_->Unlock();
      mutex_ = nullptr;
    }
  }

private:
  absl::Mutex* mutex_; // Set to nullptr on unlock, to prevent double-unlocking.
};

} // namespace Thread
} // namespace Envoy
