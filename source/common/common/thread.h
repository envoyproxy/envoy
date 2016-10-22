#pragma once

#include "envoy/thread/thread.h"

namespace Thread {

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
  static int32_t currentThreadId();

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
 * This utility class wraps the common case of having a cross-thread "one shot" ready condition.
 */
class ConditionalInitializer {
public:
  /**
   * Set the conditional to ready, should only be called once.
   */
  void setReady();

  /**
   * Block until the conditional is ready, will return immediately if it is already ready.
   *
   */
  void waitReady();

private:
  std::condition_variable cv_;
  std::mutex mutex_;
  bool ready_{false};
};

/**
 * Implementation of BasicLockable
 */
class MutexBasicLockable : public BasicLockable {
public:
  void lock() override { mutex_.lock(); }
  void unlock() override { mutex_.unlock(); }

private:
  std::mutex mutex_;
};

} // Thread
