#pragma once

#include <functional>

#include "envoy/thread/thread.h"

namespace Envoy {
namespace Thread {

/**
 * Wrapper for a win32 thread. We don't use std::thread because it eats exceptions and leads to
 * unusable stack traces.
 */
class ThreadImplWin32 : public Thread {
public:
  ThreadImplWin32(std::function<void()> thread_routine);
  ~ThreadImplWin32();

  /**
   * Join on thread exit.
   */
  void join() override;

private:
  std::function<void()> thread_routine_;
  HANDLE thread_handle_;
};

/**
 * Implementation of ThreadFactory
 */
class ThreadFactoryImplWin32 : public ThreadFactory {
public:
  ThreadFactoryImplWin32() {}

  ThreadPtr createThread(std::function<void()> thread_routine) override {
    return std::make_unique<ThreadImplWin32>(thread_routine);
  }
};

} // namespace Thread
} // namespace Envoy
