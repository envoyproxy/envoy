#include "envoy/thread/thread.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Thread {

ThreadFactory* ThreadFactorySingleton::thread_factory_{nullptr};

// This function can not be inlined in the thread.h header due to the use of ASSERT() creating a
// circular dependency with assert.h.
void ThreadFactorySingleton::set(ThreadFactory* thread_factory) {
  ASSERT(thread_factory == nullptr || thread_factory_ == nullptr);
  thread_factory_ = thread_factory;
}

} // namespace Thread
} // namespace Envoy
