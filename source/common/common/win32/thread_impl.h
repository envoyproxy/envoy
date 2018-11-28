#pragma once

#include <functional>

#include "envoy/thread/thread.h"

namespace Envoy {
namespace Thread {

/**
 * Wrapper for a win32 thread. We don't use std::thread because it eats exceptions and leads to
 * unusable stack traces.
 */
class ThreadImpl : public Thread {
public:
  ThreadImpl(std::function<void()> thread_routine);
  ~ThreadImpl();

  /**
   * Join on thread exit.
   */
  void join() override;

private:
  std::function<void()> thread_routine_;
  HANDLE thread_handle_;
};

} // namespace Thread
} // namespace Envoy
