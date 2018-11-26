#pragma once

#include <functional>
#include <memory>

#include "envoy/thread/thread.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Thread {

typedef int32_t ThreadId;

/**
 * Get current thread id.
 */
ThreadId currentThreadId();

/**
 * Implementation of ThreadFactory
 */
class ThreadFactoryImpl : public ThreadFactory {
public:
  ThreadFactoryImpl() {}

  ThreadPtr createThread(std::function<void()> thread_routine) override;
};

/**
 * Implementation of BasicLockable
 */
class MutexBasicLockable : public BasicLockable {
public:
  // BasicLockable
  void lock() EXCLUSIVE_LOCK_FUNCTION() override { mutex_.Lock(); }
  bool tryLock() EXCLUSIVE_TRYLOCK_FUNCTION(true) override { return mutex_.TryLock(); }
  void unlock() UNLOCK_FUNCTION() override { mutex_.Unlock(); }

private:
  friend class CondVar;
  absl::Mutex mutex_;
};

/**
 * Implementation of condvar, based on MutexLockable. This interface is a hybrid
 * between std::condition_variable and absl::CondVar.
 */
class CondVar {
public:
  enum class WaitStatus {
    Timeout,
    NoTimeout, // Success or Spurious
  };

  /**
   * Note that it is not necessary to be holding an associated mutex to call
   * notifyOne or notifyAll. See the discussion in
   *     http://en.cppreference.com/w/cpp/thread/condition_variable_any/notify_one
   * for more details.
   */
  void notifyOne() noexcept { condvar_.Signal(); }
  void notifyAll() noexcept { condvar_.SignalAll(); };

  /**
   * wait() and waitFor do not throw, and never will, as they are based on
   * absl::CondVar, so it's safe to pass the a mutex to wait() directly, even if
   * it's also managed by a LockGuard. See definition of CondVar in
   * source/source/thread.h for an alternate implementation, which does not work
   * with thread annotation.
   */
  void wait(MutexBasicLockable& mutex) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    condvar_.Wait(&mutex.mutex_);
  }
  template <class Rep, class Period>

  /**
   * @return WaitStatus whether the condition timed out or not.
   */
  WaitStatus
  waitFor(MutexBasicLockable& mutex,
          std::chrono::duration<Rep, Period> duration) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    return condvar_.WaitWithTimeout(&mutex.mutex_, absl::FromChrono(duration))
               ? WaitStatus::Timeout
               : WaitStatus::NoTimeout;
  }

private:
  // Note: alternate implementation of this class based on std::condition_variable_any
  // https://gist.github.com/jmarantz/d22b836cee3ca203cc368553eda81ce5
  // does not currently work well with thread-annotation.
  absl::CondVar condvar_;
};

} // namespace Thread
} // namespace Envoy
