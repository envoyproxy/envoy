#pragma once

#include <functional>

#include "envoy/thread/thread.h"

namespace Envoy {
namespace Thread {

/**
 * Wrapper for a pthread thread. We don't use std::thread because it eats exceptions and leads to
 * unusable stack traces.
 */
class ThreadImplPosix : public Thread {
public:
  ThreadImplPosix(std::function<void()> thread_routine);

  /**
   * Join on thread exit.
   */
  void join() override;

private:
  std::function<void()> thread_routine_;
  pthread_t thread_handle_;
};

/**
 * Implementation of ThreadFactory
 */
class ThreadFactoryImplPosix : public ThreadFactory {
public:
  ThreadFactoryImplPosix() {}

  ThreadPtr createThread(std::function<void()> thread_routine) override {
    return std::make_unique<ThreadImplPosix>(thread_routine);
  }
};

} // namespace Thread
} // namespace Envoy
