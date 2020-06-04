#pragma once

#include <functional>

#include "envoy/common/platform.h"
#include "envoy/thread/thread.h"

namespace Envoy {
namespace Thread {

/**
 * Implementation of ThreadFactory
 */
class ThreadFactoryImplWin32 : public ThreadFactory {
public:
  // Thread::ThreadFactory
  ThreadPtr createThread(std::function<void()> thread_routine, OptionsOptConstRef options) override;
  ThreadId currentThreadId() override;
};

} // namespace Thread
} // namespace Envoy
