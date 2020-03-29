#pragma once

#include <cassert>
#include <functional>
#include <memory>

#include "envoy/thread/thread.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Thread {

/**
 * Implementation of BasicLockable
 */
class MutexBasicLockable : public BasicLockable {
public:
  // BasicLockable
  void lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() override { mutex_.Lock(); }
  bool tryLock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true) override { return mutex_.TryLock(); }
  void unlock() ABSL_UNLOCK_FUNCTION() override { mutex_.Unlock(); }

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
  void notifyOne() noexcept {
    signaled_ = true;
    condvar_.Signal();
  }

  void notifyAll() noexcept {
    signaled_ = true;
    condvar_.SignalAll();
  }

  bool signaled() {
    return signaled_;
  }

  void clearSignaled() {
    //assert(signaled_ > 0);
    signaled_ = false;
  }

  /**
   * wait() and waitFor do not throw, and never will, as they are based on
   * absl::CondVar, so it's safe to pass the a mutex to wait() directly, even if
   * it's also managed by a LockGuard. See definition of CondVar in
   * source/source/thread.h for an alternate implementation, which does not work
   * with thread annotation.
   */
  void wait(MutexBasicLockable& mutex) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    //if (!signaled_) {
    condvar_.Wait(&mutex.mutex_);
      // }
    clearSignaled();
  }

  /**
   * @return WaitStatus whether the condition timed out or not.
   */
  template <class Rep, class Period>
  WaitStatus waitFor(
      MutexBasicLockable& mutex,
      std::chrono::duration<Rep, Period> duration) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    /*
    if (!signaled_) {
      // Note we ignore the return value from absl condvar here as it's spurious
      // anyway.
      condvar_.WaitWithTimeout(&mutex.mutex_, absl::FromChrono(duration));
    }
    if (signaled_) {
      signaled_ = false;
      return WaitStatus::NoTimeout;
    }
    return WaitStatus::Timeout;
    */
    bool timeout = condvar_.WaitWithTimeout(&mutex.mutex_, absl::FromChrono(duration));
    if (timeout) {
      return WaitStatus::Timeout;
    }
    clearSignaled();
    return WaitStatus::NoTimeout;
  }

private:
  // Note: alternate implementation of this class based on std::condition_variable_any
  // https://gist.github.com/jmarantz/d22b836cee3ca203cc368553eda81ce5
  // does not currently work well with thread-annotation.
  absl::CondVar condvar_;
  std::atomic<bool> signaled_{false};
};

} // namespace Thread
} // namespace Envoy
