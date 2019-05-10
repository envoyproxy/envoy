#include "common/common/thread_impl.h"
#include "test/test_common/thread_factory_for_test.h"
#include "absl/container/flat_hash_set.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(ThreadIdImplTest, TestHash) {
  Thread::ThreadFactory& factory = Thread::threadFactoryForTest();
  Thread::ThreadIdPtr current_thread = factory.currentThreadId();
  absl::flat_hash_set<Thread::ThreadId> thread_ids;
  // thread_ids.insert(*current_thread);
  // thread_ids.(*current_thread);
}

} // namespace Envoy
