#include "common/common/thread.h"

#ifdef __linux__
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif

#include <functional>

#include "common/common/assert.h"
#include "common/common/macros.h"

namespace Envoy {
namespace Thread {

/**
 * Wrapper for a pthread thread. We don't use std::thread because it eats exceptions and leads to
 * unusable stack traces.
 */
class ThreadImpl : public Thread {
public:
  ThreadImpl(std::function<void()> thread_routine) : thread_routine_(thread_routine) {
    RELEASE_ASSERT(Logger::Registry::initialized(), "");
    int rc = pthread_create(&thread_id_, nullptr,
                            [](void* arg) -> void* {
                              static_cast<ThreadImpl*>(arg)->thread_routine_();
                              return nullptr;
                            },
                            this);
    RELEASE_ASSERT(rc == 0, "");
  }

  void join() override {
    int rc = pthread_join(thread_id_, nullptr);
    RELEASE_ASSERT(rc == 0, "");
  }

private:
  std::function<void()> thread_routine_;
  pthread_t thread_id_;
};

ThreadPtr ThreadFactoryImpl::createThread(std::function<void()> thread_routine) {
  return std::make_unique<ThreadImpl>(thread_routine);
}

int32_t currentThreadId() {
#ifdef __linux__
  return syscall(SYS_gettid);
#elif defined(__APPLE__)
  uint64_t tid;
  pthread_threadid_np(NULL, &tid);
  return static_cast<int32_t>(tid);
#else
#error "Enable and test pthread id retrieval code for you arch in thread.cc"
#endif
}

} // namespace Thread
} // namespace Envoy
