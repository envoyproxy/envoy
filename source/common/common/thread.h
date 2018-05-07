#pragma once

#include <functional>
#include <memory>
#include <mutex>

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
  void lock() override { mutex_.lock(); }
  bool try_lock() override { return mutex_.try_lock(); }
  void unlock() override { mutex_.unlock(); }

private:
  // TODO(jmarantz): change to absl::Mutex and add thread annotations.
  std::mutex mutex_;
};

} // namespace Thread
} // namespace Envoy
