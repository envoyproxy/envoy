#include "assert.h"
#include "thread.h"

namespace Thread {

Thread::Thread(std::function<void()> thread_routine) : thread_routine_(thread_routine) {
  int rc = pthread_create(&thread_id_, nullptr, [](void* arg) -> void* {
    static_cast<Thread*>(arg)->thread_routine_();
    return nullptr;
  }, this);
  RELEASE_ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

void Thread::join() {
  int rc = pthread_join(thread_id_, nullptr);
  RELEASE_ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

void ConditionalInitializer::setReady() {
  std::unique_lock<std::mutex> lock(mutex_);
  ASSERT(!ready_);
  ready_ = true;
  cv_.notify_all();
}

void ConditionalInitializer::waitReady() {
  std::unique_lock<std::mutex> lock(mutex_);
  if (ready_) {
    return;
  }

  cv_.wait(lock);
  ASSERT(ready_);
}

} // Thread
