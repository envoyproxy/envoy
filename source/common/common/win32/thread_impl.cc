#include <process.h>

#include "common/common/assert.h"
#include "common/common/thread_impl.h"

namespace Envoy {
namespace Thread {

ThreadIdImplWin32::ThreadIdImplWin32(DWORD id) : id_(id) {}

std::string ThreadIdImplWin32::debugString() const { return std::to_string(id_); }

bool ThreadIdImplWin32::isCurrentThreadId() const { return id_ == ::GetCurrentThreadId(); }

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

ThreadIdPtr ThreadFactoryImplWin32::currentThreadId() {
  return std::make_unique<ThreadIdImplWin32>(::GetCurrentThreadId());
}

} // namespace Thread
} // namespace Envoy
