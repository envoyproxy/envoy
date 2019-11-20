#include "common/common/assert.h"
#include "common/common/thread_impl.h"

#if defined(__linux__)
#include <sys/syscall.h>
#endif

namespace Envoy {
namespace Thread {

namespace {

int64_t getCurrentThreadId() {
#ifdef __linux__
  return static_cast<int64_t>(syscall(SYS_gettid));
#elif defined(__APPLE__)
  uint64_t tid;
  pthread_threadid_np(nullptr, &tid);
  return tid;
#else
#error "Enable and test pthread id retrieval code for you arch in pthread/thread_impl.cc"
#endif
}

} // namespace

ThreadImplPosix::ThreadImplPosix(std::function<void()> thread_routine)
    : thread_routine_(std::move(thread_routine)) {
  RELEASE_ASSERT(Logger::Registry::initialized(), "");
  const int rc = pthread_create(
      &thread_handle_, nullptr,
      [](void* arg) -> void* {
        static_cast<ThreadImplPosix*>(arg)->thread_routine_();
        return nullptr;
      },
      this);
  RELEASE_ASSERT(rc == 0, "");
}

void ThreadImplPosix::join() {
  const int rc = pthread_join(thread_handle_, nullptr);
  RELEASE_ASSERT(rc == 0, "");
}

ThreadPtr ThreadFactoryImplPosix::createThread(std::function<void()> thread_routine) {
  return std::make_unique<ThreadImplPosix>(thread_routine);
}

ThreadId ThreadFactoryImplPosix::currentThreadId() { return ThreadId(getCurrentThreadId()); }

} // namespace Thread
} // namespace Envoy
