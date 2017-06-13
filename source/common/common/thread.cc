#include "common/common/thread.h"

#include <sys/syscall.h>

#include <functional>

#include "common/common/assert.h"
#include "common/common/macros.h"

namespace Envoy {
namespace Thread {

Thread::Thread(std::function<void()> thread_routine) : thread_routine_(std::move(thread_routine)) {
  int rc = pthread_create(&thread_id_, nullptr, [](void* arg) -> void* {
    static_cast<Thread*>(arg)->thread_routine_();
    return nullptr;
  }, this);
  RELEASE_ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

int32_t Thread::currentThreadId() { return syscall(SYS_gettid); }

void Thread::join() {
  int rc = pthread_join(thread_id_, nullptr);
  RELEASE_ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

} // Thread
} // Envoy
