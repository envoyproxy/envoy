#include <functional>

#include "common/common/thread.h"
#include "common/common/thread_synchronizer.h"

#include "test/test_common/thread_factory_for_test.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Thread {

class ThreadTest : public testing::Test {
protected:
  ThreadFactory& thread_factory_{threadFactoryForTest()};
};

// Tests that two threads racing to create an object have well-defined
// behavior.
TEST_F(ThreadTest, AtomicPtr) {
  AtomicPtr<std::string, AtomicPtrAllocMode::DeleteOnDestruct> str;
  ThreadSynchronizer sync;
  sync.enable();
  sync.waitOn("creator");

  // On thread1, we will lazily instantiate the string as "thread1". However
  // in the creation function we will block on a sync-point.
  auto thread1 = thread_factory_.createThread([&str, &sync]() {
    str.get([&sync]() -> std::string* {
      sync.syncPoint("creator");
      return new std::string("thread1");
    });
  });

  sync.barrierOn("creator");

  // Now spawn a separate thread that will attempt to lazy-initialize the
  // string as "thread2", but that allocator will never run because
  // the allocator on thread1 has already locked the AtomicPtr's mutex.
  auto thread2 = thread_factory_.createThread(
      [&str]() { str.get([]() -> std::string* { return new std::string("thread2"); }); });

  // Now let thread1's initializer finish.
  sync.signal("creator");
  thread1->join();
  thread2->join();

  // NOow enuure the "thread1" value sticks past the thread lifetimes.
  bool called = false;
  EXPECT_EQ("thread1", *str.get([&called]() -> std::string* { called = true; return nullptr; }));
  EXPECT_FALSE(called);
}

// Tests that null can be allocated, but the allocator will be re-called each
// time until a non-null result is returned.
TEST_F(ThreadTest, Null) {
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
TEST_F(ThreadTest, Array) {
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

TEST_F(ThreadTest, ManagedAlloc) {
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

} // namespace Thread
} // namespace Envoy
