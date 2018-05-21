#pragma once

#include <functional>
#include <memory>

#include "envoy/thread/thread.h"

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
  void lock() EXCLUSIVE_LOCK_FUNCTION() override { mutex_.Lock(); }
  bool tryLock() EXCLUSIVE_TRYLOCK_FUNCTION(true) override { return mutex_.TryLock(); }
  void unlock() UNLOCK_FUNCTION() override { mutex_.Unlock(); }

private:
  absl::Mutex mutex_;
};

/**
 * Implementation of condvar, based on MutexLockable.
 */
class CondVar {
  virtual ~CondVar() {}

  virtual void notifyOne() PURE {};
  virtual void notifyAll() PURE {};
  virtual void wait() PURE {};
  template<class Predicate> virtual void wait(Predicate) PURE {};
  virtual void waitForMs(int64 durationMs) PURE {};

} // namespace Thread
} // namespace Envoy
