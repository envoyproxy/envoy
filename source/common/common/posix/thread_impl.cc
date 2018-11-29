#include "common/common/assert.h"
#include "common/common/thread_impl.h"

#ifdef __linux__
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif

namespace Envoy {
namespace Thread {

ThreadImplPosix::ThreadImplPosix(std::function<void()> thread_routine)
    : thread_routine_(thread_routine) {
  RELEASE_ASSERT(Logger::Registry::initialized(), "");
  const int rc = pthread_create(&thread_handle_, nullptr,
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

ThreadId currentThreadId() {
#ifdef __linux__
  return syscall(SYS_gettid);
#elif defined(__APPLE__)
  uint64_t tid;
  pthread_threadid_np(NULL, &tid);
  return static_cast<int32_t>(tid);
#else
#error "Enable and test pthread id retrieval code for you arch in pthread/thread_impl.cc"
#endif
}

} // namespace Thread
} // namespace Envoy
