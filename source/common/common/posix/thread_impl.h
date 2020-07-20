#pragma once

#include <pthread.h>

#include <functional>

#include "envoy/thread/thread.h"

namespace Envoy {
namespace Thread {

/**
 * Implementation of ThreadFactory
 */
class ThreadFactoryImplPosix : public ThreadFactory {
public:
  // Thread::ThreadFactory
  ThreadPtr createThread(std::function<void()> thread_routine, OptionsOptConstRef options) override;
  ThreadId currentThreadId() override;
};

} // namespace Thread
} // namespace Envoy
