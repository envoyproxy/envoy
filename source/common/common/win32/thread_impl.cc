#include <process.h>

#include "source/common/common/assert.h"
#include "source/common/common/thread_impl.h"

namespace Envoy {
namespace Thread {

ThreadImplWin32::ThreadImplWin32(std::function<void()> thread_routine, OptionsOptConstRef options)
    : thread_routine_(thread_routine) {
  if (options) {
    name_ = options->name_;
    // TODO(jmarantz): set the thread name for task manager, etc, or pull the
    // auto-generated name from the OS if options is not present.
  }

  RELEASE_ASSERT(Logger::Registry::initialized(), "");
  thread_handle_ = reinterpret_cast<HANDLE>(::_beginthreadex(
      nullptr, 0,
      [](void* arg) -> unsigned int {
        static_cast<ThreadImplWin32*>(arg)->thread_routine_();
        return 0;
      },
      this, 0, nullptr));
  if (options && options.thread_priority_ &&
      !SetThreadPriority(thread_handle_, *options.thread_priority_)) {
    ENVOY_LOG_MISC(warn, "Could not set the thread priority to {}", *options.thread_priority_);
  }
  RELEASE_ASSERT(thread_handle_ != 0, "");
}

ThreadImplWin32::~ThreadImplWin32() { ::CloseHandle(thread_handle_); }

void ThreadImplWin32::join() {
  const DWORD rc = ::WaitForSingleObject(thread_handle_, INFINITE);
  RELEASE_ASSERT(rc == WAIT_OBJECT_0, "");
}

ThreadPtr ThreadFactoryImplWin32::createThread(std::function<void()> thread_routine,
                                               OptionsOptConstRef options) {
  return std::make_unique<ThreadImplWin32>(thread_routine, options);
}

ThreadId ThreadFactoryImplWin32::currentThreadId() {
  // TODO(mhoran): test this in windows please.
  return ThreadId(static_cast<int64_t>(::GetCurrentThreadId()));
}

} // namespace Thread
} // namespace Envoy
