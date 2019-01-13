#include "envoy/thread/thread.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Thread {

ThreadFactory* ThreadFactorySingleton::thread_factory_{nullptr};

// This function can not be inlined in the thread.h header due to the use of ASSERT() creating a
// circular dependency with assert.h.
void ThreadFactorySingleton::set(ThreadFactory* thread_factory) {
  // Verify that either the singleton is uninitialized (i.e., thread_factory_ == nullptr) OR it's
  // being reset to the uninitialized state (i.e., thread_factory == nullptr), but _not_ both. The
  // use of XOR complicates tests but improves our ability to catch init/cleanup errors.
  ASSERT((thread_factory == nullptr) != (thread_factory_ == nullptr));
  thread_factory_ = thread_factory;
}

} // namespace Thread
} // namespace Envoy
