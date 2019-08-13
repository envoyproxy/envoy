#include <process.h>

#include "common/common/assert.h"
#include "common/common/thread_impl.h"

namespace Envoy {
namespace Thread {

ThreadImplWin32::ThreadImplWin32(std::function<void()> thread_routine)
    : thread_routine_(thread_routine) {
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

ThreadImplWin32::~ThreadImplWin32() { ::CloseHandle(thread_handle_); }

void ThreadImplWin32::join() {
  const DWORD rc = ::WaitForSingleObject(thread_handle_, INFINITE);
  RELEASE_ASSERT(rc == WAIT_OBJECT_0, "");
}

ThreadPtr ThreadFactoryImplWin32::createThread(std::function<void()> thread_routine) {
  return std::make_unique<ThreadImplWin32>(thread_routine);
}

ThreadId ThreadFactoryImplWin32::currentThreadId() {
  // TODO(mhoran): test this in windows please.
  return ThreadId(static_cast<int64_t>(::GetCurrentThreadId()));
}

} // namespace Thread
} // namespace Envoy
