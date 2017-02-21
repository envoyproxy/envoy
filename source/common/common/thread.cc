#include "common/common/thread.h"

#ifdef linux
#include <sys/syscall.h>
#elif defined(__FreeBSD__)
#include <pthread_np.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

#include <functional>

#include "common/common/assert.h"
#include "common/common/macros.h"

namespace Envoy {
namespace Thread {

Thread::Thread(std::function<void()> thread_routine) : thread_routine_(thread_routine) {
  int rc = pthread_create(&thread_id_, nullptr,
                          [](void* arg) -> void* {
                            static_cast<Thread*>(arg)->thread_routine_();
                            return nullptr;
                          },
                          this);
  RELEASE_ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

int32_t Thread::currentThreadId() {
#ifdef linux
  return syscall(SYS_gettid);
#elif defined(__FreeBSD__)
  return pthread_getthreadid_np();
#elif defined(__APPLE__)
  int ret = mach_thread_self();
  mach_port_deallocate(mach_task_self(), ret);
  return ret;
#endif
}

void Thread::join() {
  int rc = pthread_join(thread_id_, nullptr);
  RELEASE_ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

} // namespace Thread
} // namespace Envoy
