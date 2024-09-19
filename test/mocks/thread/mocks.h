#pragma once

#include "envoy/thread/thread.h"

#include "gmock/gmock.h"

#if defined(__linux__) || defined(__APPLE__)
#include "source/common/common/posix/thread_impl.h"
#endif

namespace Envoy {
namespace Thread {

class MockThreadFactory : public ThreadFactory {
public:
  MOCK_METHOD(ThreadPtr, createThread, (std::function<void()>, OptionsOptConstRef));
  MOCK_METHOD(ThreadId, currentThreadId, (), (const));
};

#if defined(__linux__) || defined(__APPLE__)
class MockPosixThreadFactory : public PosixThreadFactory {
public:
  MOCK_METHOD(ThreadPtr, createThread, (std::function<void()>, OptionsOptConstRef));
  MOCK_METHOD(PosixThreadPtr, createThread,
              (std::function<void()>, OptionsOptConstRef, bool crash_on_failure));
  MOCK_METHOD(ThreadId, currentThreadId, (), (const));
  MOCK_METHOD(ThreadId, currentPthreadId, (), (const));
};
#endif

} // namespace Thread
} // namespace Envoy
