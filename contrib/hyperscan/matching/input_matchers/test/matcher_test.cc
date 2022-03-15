#ifndef HYPERSCAN_DISABLED
#include "test/test_common/utility.h"

#include "absl/synchronization/blocking_counter.h"
#include "contrib/hyperscan/matching/input_matchers/source/matcher.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

// Verify that we do not get TSAN or other errors when creating scratch in
// multithreading.
TEST(MatcherTest, RaceScratchCreation) {
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();

  hs_database_t* database;
  hs_compile_error_t* compile_err;
  hs_error_t err = hs_compile("hello", 0, HS_MODE_BLOCK, nullptr, &database, &compile_err);
  ASSERT(err == HS_SUCCESS);

  constexpr int num_threads = 100;
  std::vector<Thread::ThreadPtr> threads;
  threads.reserve(num_threads);
  ConditionalInitializer creation, wait;
  absl::BlockingCounter creates(num_threads);
  for (int i = 0; i < num_threads; ++i) {
    threads.push_back(thread_factory.createThread([&database, &creation, &wait, &creates]() {
      // Block each thread on waking up a common condition variable,
      // so we make it likely to race on creation.
      creation.wait();
      ScratchThreadLocal tls(database);
      creates.DecrementCount();

      wait.wait();
    }));
  }
  creation.setReady();
  creates.Wait();

  wait.setReady();
  for (auto& thread : threads) {
    thread->join();
  }
}

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
#endif
