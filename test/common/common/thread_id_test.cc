#include "source/common/common/thread.h"

#include "test/test_common/thread_factory_for_test.h"

#include "absl/hash/hash_testing.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace {

TEST(ThreadId, Equality) {
  auto& thread_factory = Thread::threadFactoryForTest();

  Thread::ThreadId main_thread = thread_factory.currentThreadId();
  Thread::ThreadId background_thread;
  Thread::ThreadId null_thread;

  Thread::threadFactoryForTest()
      .createThread([&]() { background_thread = thread_factory.currentThreadId(); })
      ->join();

  EXPECT_EQ(main_thread, main_thread);
  EXPECT_EQ(background_thread, background_thread);
  EXPECT_EQ(null_thread, null_thread);

  EXPECT_NE(main_thread, background_thread);
  EXPECT_NE(main_thread, null_thread);
  EXPECT_NE(background_thread, null_thread);
}

TEST(ThreadId, Hashability) {
  auto& thread_factory = Thread::threadFactoryForTest();

  Thread::ThreadId main_thread = thread_factory.currentThreadId();
  Thread::ThreadId background_thread;
  Thread::ThreadId null_thread;

  Thread::threadFactoryForTest()
      .createThread([&]() { background_thread = thread_factory.currentThreadId(); })
      ->join();

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      null_thread,
      main_thread,
      background_thread,
  }));
}

TEST(ThreadId, CanGetId) {
  Thread::ThreadId tid(10);
  EXPECT_EQ(tid.getId(), 10);
}

} // namespace
} // namespace Envoy
