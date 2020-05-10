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

TEST_F(ThreadTest, AtomicObject) {
  AtomicObject<std::string> str;
  ThreadSynchronizer sync;
  sync.enable();
  sync.waitOn("creator");

  // On thread1, we will lazily instantiate the string as "thread1". However
  // in the creation function we will block on a sync-point.
  auto thread1 = thread_factory_.createThread([&str,&sync]() {
    str.get([&sync]() -> std::string* {
      sync.syncPoint("creator");
      return new std::string("thread1");
    });
  });

  sync.barrierOn("creator");

  // Now spawn a separate thread that will attempt to lazy-initialize the
  // string as "thread2", but that initializer will never run because
  // the inializer on thread1 has already locked the AtomicObject's mutex.
  auto thread2 = thread_factory_.createThread([&str]() {
    str.get([]() -> std::string* {
      return new std::string("thread2");
    });
  });

  // Now let thread1's initializer finish.
  sync.signal("creator");
  thread1->join();
  thread2->join();
  EXPECT_EQ("thread1", *str.get([]() -> std::string* { return new std::string("mainline"); }));
}

} // namespace Thread
} // namespace Envoy
