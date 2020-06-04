#include <process.h>

#include "common/common/assert.h"
#include "common/common/thread_impl.h"

namespace Envoy {
namespace Thread {

/**
 * Wrapper for a win32 thread. We don't use std::thread because it eats exceptions and leads to
 * unusable stack traces.
 */
class ThreadImplWin32 : public Thread {
public:
  ThreadImplWin32(std::function<void()> thread_routine, OptionsOptConstRef options)
      : thread_routine_(thread_routine) {
    UNREFERENCED_PARAMETER(options); // TODO(jmarantz): set the thread name for task manager, etc.
    RELEASE_ASSERT(Logger::Registry::initialized(), "");
    thread_handle_ = reinterpret_cast<HANDLE>(::_beginthreadex(
        nullptr, 0,
        [](void* arg) -> unsigned int {
          static_cast<ThreadImplWin32*>(arg)->thread_routine_();
          return 0;
        },
        this, 0, nullptr));
    RELEASE_ASSERT(thread_handle_ != 0, "");
  }

  ~ThreadImplWin32() { ::CloseHandle(thread_handle_); }

  // Thread::Thread
  void join() override {
    const DWORD rc = ::WaitForSingleObject(thread_handle_, INFINITE);
    RELEASE_ASSERT(rc == WAIT_OBJECT_0, "");
  }

  // Needed for WatcherImpl for the QueueUserAPC callback context
  HANDLE handle() const { return thread_handle_; }

private:
  std::function<void()> thread_routine_;
  HANDLE thread_handle_;
};

ThreadPtr ThreadFactoryImplWin32::createThread(std::function<void()> thread_routine,
                                               OptionsOptConstRef options) {
  return std::make_unique<ThreadImplWin32>(thread_routine, name);
}

ThreadId ThreadFactoryImplWin32::currentThreadId() {
  // TODO(mhoran): test this in windows please.
  return ThreadId(static_cast<int64_t>(::GetCurrentThreadId()));
}

} // namespace Thread
} // namespace Envoy
