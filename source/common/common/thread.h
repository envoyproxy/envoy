#pragma once

#include <functional>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/thread/thread.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Thread {

/**
 * Implementation of BasicLockable
 */
class MutexBasicLockable : public BasicLockable {
public:
  using BoolFn = std::function<bool()>;
  using Duration = MonotonicTime::duration;

  // BasicLockable
  void lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() override { mutex_.Lock(); }
  bool tryLock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true) override { return mutex_.TryLock(); }
  void unlock() ABSL_UNLOCK_FUNCTION() override { mutex_.Unlock(); }

  bool awaitWithTimeout(const bool& condition, const Duration& timeout) {
    return mutex_.AwaitWithTimeout(absl::Condition(&condition), absl::FromChrono(timeout));
  }
  bool awaitWithTimeout(BoolFn check_condition, const Duration& timeout) {
    auto cond_no_capture = +[](BoolFn* check_condition) -> bool { return (*check_condition)(); };
    return mutex_.AwaitWithTimeout(absl::Condition(cond_no_capture, &check_condition),
                                   absl::FromChrono(timeout));
  }

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
  void wait(MutexBasicLockable& mutex) noexcept ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    condvar_.Wait(&mutex.mutex_);
  }

  /**
   * @return WaitStatus whether the condition timed out or not.
   */
  template <class Rep, class Period>
  WaitStatus waitFor(
      MutexBasicLockable& mutex,
      std::chrono::duration<Rep, Period> duration) noexcept ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
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
