#pragma once

#include <functional>

#include "envoy/common/platform.h"
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

  // Thread::Thread
  void join() override;

  // Needed for WatcherImpl for the QueueUserAPC callback context
  HANDLE handle() const { return thread_handle_; }

private:
  std::function<void()> thread_routine_;
  HANDLE thread_handle_;
};

/**
 * Implementation of ThreadFactory
 */
class ThreadFactoryImplWin32 : public ThreadFactory {
public:
  // Thread::ThreadFactory
  ThreadPtr createThread(std::function<void()> thread_routine) override;
  ThreadId currentThreadId() override;
};

} // namespace Thread
} // namespace Envoy
