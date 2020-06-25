#include <functional>

#include "common/common/thread.h"
#include "common/common/thread_synchronizer.h"

#include "test/test_common/thread_factory_for_test.h"

#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Thread {
namespace {

class ThreadAsyncPtrTest : public testing::Test {
protected:
  ThreadFactory& thread_factory_{threadFactoryForTest()};
};

// Tests that two threads racing to create an object have well-defined
// behavior.
TEST_F(ThreadAsyncPtrTest, DeleteOnDestruct) {
  AtomicPtr<std::string, AtomicPtrAllocMode::DeleteOnDestruct> str;
  ThreadSynchronizer sync;
  sync.enable();
  sync.waitOn("creator");

  // On thread1, we will lazily instantiate the string as "thread1". However
  // in the creation function we will block on a sync-point.
  auto thread1 = thread_factory_.createThread(
      [&str, &sync]() {
        str.get([&sync]() -> std::string* {
          sync.syncPoint("creator");
          return new std::string("thread1");
        });
      },
      Options{"thread1"});
  EXPECT_EQ("thread1", thread1->name());

  sync.barrierOn("creator");

  // Now spawn a separate thread that will attempt to lazy-initialize the
  // string as "thread2", but that allocator will never run because
  // the allocator on thread1 has already locked the AtomicPtr's mutex.
  auto thread2 = thread_factory_.createThread(
      [&str]() { str.get([]() -> std::string* { return new std::string("thread2"); }); },
      Options{"thread2"});
  EXPECT_EQ("thread2", thread2->name());

  // Now let thread1's initializer finish.
  sync.signal("creator");
  thread1->join();
  thread2->join();

  // Now ensure the "thread1" value sticks past the thread lifetimes.
  bool called = false;
  EXPECT_EQ("thread1", *str.get([&called]() -> std::string* {
    called = true;
    return nullptr;
  }));
  EXPECT_FALSE(called);
}

// Same test as AtomicPtrDeleteOnDestruct, except the allocator callbacks return
// pointers to locals, rather than allocating the strings on the heap.
TEST_F(ThreadAsyncPtrTest, DoNotDelete) {
  const std::string thread1_str("thread1");
  const std::string thread2_str("thread2");
  AtomicPtr<const std::string, AtomicPtrAllocMode::DoNotDelete> str;
  ThreadSynchronizer sync;
  sync.enable();
  sync.waitOn("creator");

  // On thread1, we will lazily instantiate the string as "thread1". However
  // in the creation function we will block on a sync-point.
  auto thread1 = thread_factory_.createThread(
      [&str, &sync, &thread1_str]() {
        str.get([&sync, &thread1_str]() -> const std::string* {
          sync.syncPoint("creator");
          return &thread1_str;
        });
      },
      Options{"thread1"});

  sync.barrierOn("creator");

  // Now spawn a separate thread that will attempt to lazy-initialize the
  // string as "thread2", but that allocator will never run because
  // the allocator on thread1 has already locked the AtomicPtr's mutex.
  auto thread2 = thread_factory_.createThread(
      [&str, &thread2_str]() {
        str.get([&thread2_str]() -> const std::string* { return &thread2_str; });
      },
      Options{"thread2"});

  // Now let thread1's initializer finish.
  sync.signal("creator");
  thread1->join();
  thread2->join();

  // Now ensure the "thread1" value sticks past the thread lifetimes.
  bool called = false;
  EXPECT_EQ("thread1", *str.get([&called]() -> std::string* {
    called = true;
    return nullptr;
  }));
  EXPECT_FALSE(called);
}

TEST_F(ThreadAsyncPtrTest, ThreadSpammer) {
  AtomicPtr<std::string, AtomicPtrAllocMode::DeleteOnDestruct> str;
  absl::Notification go;
  constexpr uint32_t num_threads = 100;
  AtomicPtr<uint32_t, AtomicPtrAllocMode::DeleteOnDestruct> answer;
  uint32_t calls = 0;
  auto thread_fn = [&go, &answer, &calls]() {
    go.WaitForNotification();
    answer.get([&calls]() {
      ++calls;
      return new uint32_t(42);
    });
  };
  std::vector<ThreadPtr> threads;
  for (uint32_t i = 0; i < num_threads; ++i) {
    std::string name = absl::StrCat("thread", i);
    threads.emplace_back(thread_factory_.createThread(thread_fn, Options{name}));
    EXPECT_EQ(name, threads.back()->name());
  }
  EXPECT_EQ(0, calls);
  go.Notify();
  for (auto& thread : threads) {
    thread->join();
  }
  EXPECT_EQ(1, calls);
  EXPECT_EQ(42, *answer.get([&calls]() {
    ++calls;
    return nullptr;
  }));
  EXPECT_EQ(1, calls);
}

// Tests that null can be allocated, but the allocator will be re-called each
// time until a non-null result is returned.
TEST_F(ThreadAsyncPtrTest, Null) {
  AtomicPtr<std::string, AtomicPtrAllocMode::DeleteOnDestruct> str;
  uint32_t calls = 0;
  EXPECT_EQ(nullptr, str.get([&calls]() -> std::string* {
    ++calls;
    return nullptr;
  }));
  EXPECT_EQ(nullptr, str.get([&calls]() -> std::string* {
    ++calls;
    return nullptr;
  }));
  EXPECT_EQ(2, calls);
  EXPECT_EQ("x", *str.get([&calls]() -> std::string* {
    ++calls;
    return new std::string("x");
  }));
  EXPECT_EQ(3, calls);
  EXPECT_EQ("x", *str.get([&calls]() -> std::string* {
    ++calls;
    return nullptr;
  }));
  EXPECT_EQ(3, calls); // allocator was not called this last time.
}

// Tests array semantics. Note that AtomicPtr is implemented a 1-element
// AtomicPtrArray, so there's no need to repeat the complex thread-race test
// from AtomicPtr.
TEST_F(ThreadAsyncPtrTest, Array) {
  const uint32_t size = 5;
  AtomicPtrArray<std::string, size, AtomicPtrAllocMode::DeleteOnDestruct> strs;
  for (uint32_t i = 0; i < size; ++i) {
    std::string val = absl::StrCat("x", i);
    EXPECT_EQ(val, *strs.get(i, [&val]() -> std::string* { return new std::string(val); }));
  }
  for (uint32_t i = 0; i < size; ++i) {
    std::string val = absl::StrCat("x", i);
    // Second time through the array, the allocator will not be called, but
    // we'll have all the expected values returned from get.
    bool called = false;
    EXPECT_EQ(val, *strs.get(i, [&called]() -> std::string* {
      called = true;
      return nullptr;
    }));
    EXPECT_FALSE(called);
  }
}

TEST_F(ThreadAsyncPtrTest, ManagedAlloc) {
  const uint32_t size = 5;
  std::vector<std::unique_ptr<std::string>> pool;
  AtomicPtrArray<std::string, size, AtomicPtrAllocMode::DoNotDelete> strs;
  for (uint32_t i = 0; i < size; ++i) {
    std::string val = absl::StrCat("x", i);
    EXPECT_EQ(val, *strs.get(i, [&pool, &val]() -> std::string* {
      pool.emplace_back(std::make_unique<std::string>(val));
      return pool.back().get();
    }));
  }
}

TEST_F(ThreadAsyncPtrTest, TruncateWait) {
  absl::Notification notify;
  auto thread = thread_factory_.createThread([&notify]() { notify.WaitForNotification(); },
                                             Options{"this name is way too long for posix"});
  notify.Notify();

  // To make this test work on multiple platforms, just assume the first 10 characters
  // are retained.
  EXPECT_THAT(thread->name(), testing::StartsWith("this name "));
  thread->join();
}

TEST_F(ThreadAsyncPtrTest, TruncateNoWait) {
  auto thread =
      thread_factory_.createThread([]() {}, Options{"this name is way too long for posix"});

  // In general, across platforms, just assume the first 10 characters are
  // retained.
  EXPECT_THAT(thread->name(), testing::StartsWith("this name "));

  // On Linux we can check for 15 exactly.
#ifdef __linux__
  EXPECT_EQ("this name is wa", thread->name()) << "truncated to 15 chars";
#endif

  thread->join();
}

TEST_F(ThreadAsyncPtrTest, NameNotSpecifiedWait) {
  absl::Notification notify;
  auto thread = thread_factory_.createThread([&notify]() { notify.WaitForNotification(); });
  notify.Notify();

  // For linux builds, the thread name defaults to the name of the
  // binary. However the name of the binary is different depending on whether
  // this is a coverage test or not. Currently, this population does not occur
  // for Mac or Windows.
#ifdef __linux__
  EXPECT_FALSE(thread->name().empty());
#endif
  thread->join();
}

} // namespace
} // namespace Thread
} // namespace Envoy
