#include <atomic>
#include <iostream>
#include <string>
#include <thread>

#include "common/common/thread.h"
#include "common/log_sink/conveyor.h"

#include "gtest/gtest.h"

namespace Envoy {

namespace LogTransport {

TEST(BasicConveyor, ConveyorExpandContractTest) {
  Conveyor<int> conveyor;
  conveyor.expand(1);
  EXPECT_EQ(conveyor.contract(1), 1);
}

TEST(Conveyor, ConveyorFullContractTest) {
  Conveyor<int> conveyor;
  conveyor.expand(1);
  EXPECT_EQ(conveyor.contract(20), 1);
}

TEST(Conveyor, ConveyorGetTest) {
  Conveyor<int> conveyor;
  auto should_be_unique_null = conveyor.getEmpty();
  EXPECT_EQ(should_be_unique_null.get(), nullptr);
  conveyor.expand(1);

  auto should_be_unique_valid = conveyor.getEmpty();
  EXPECT_NE(should_be_unique_valid.get(), nullptr);

  should_be_unique_null = conveyor.getEmpty();
  EXPECT_EQ(should_be_unique_null.get(), nullptr);
}

TEST(BasicConveyor, BidirectionalConveyorTest) {
  Conveyor<int> conveyor;
  conveyor.expand(20);

  for (int i = 0; i < 20; i++) {
    auto uptr = conveyor.getEmpty();
    ASSERT_TRUE(uptr);
    *uptr = i;
    conveyor.putLoaded(std::move(uptr));
  }
  for (int i = 0; i < 20; i++) {
    auto uptr = conveyor.waitAndGetLoaded();
    ASSERT_TRUE(uptr);
    ASSERT_EQ(*uptr, i);
    conveyor.putEmpty(std::move(uptr));
  }

  // Now verify that we can reuse the put objects:
  for (int i = 30; i < 50; i++) {
    auto uptr = conveyor.getEmpty();
    ASSERT_TRUE(uptr);
    *uptr = i;
    conveyor.putLoaded(std::move(uptr));
  }
  for (int i = 30; i < 50; i++) {
    auto uptr = conveyor.waitAndGetLoaded();
    ASSERT_TRUE(uptr);
    ASSERT_EQ(*uptr, i);
    conveyor.putEmpty(std::move(uptr));
  }

  // There should be exactly 20 items, even if we try to contract by more:
  EXPECT_EQ(conveyor.contract(50), 20);
}

TEST(BasicConveyor, ConstructorArgsTest) {
  Conveyor<int> conveyor;
  conveyor.expand(1, 1234);
  auto item = conveyor.getEmpty();
  EXPECT_EQ(*item, 1234);
}

class MultiThreadConveyor : public testing::Test {
protected:
  MultiThreadConveyor()
      : endpoint_thread_([this]() -> void {
          int expected_item_id = 1;
          while (auto item = conveyor_.waitAndGetLoaded()) {
            // Because of thread timing the inserter might not be able to get an
            // empty item, so we expect that the ID is monotonically increasing but
            // gaps are OK
            EXPECT_GE(*item, expected_item_id++);
            *item = 0;
            conveyor_.putEmpty(std::move(item));
          }
          std::lock_guard<std::mutex> guard(thread_exit_lock_);
          thread_running_ = false;
          thread_exit_.notify_one();
        }) {}
  Conveyor<int> conveyor_;
  Thread::Thread endpoint_thread_;
  std::mutex thread_exit_lock_;
  std::condition_variable_any thread_exit_;
  std::atomic<bool> thread_running_{true};
};

TEST_F(MultiThreadConveyor, ConveyorEchoTest) {
  conveyor_.expand(100);
  // Start testing at one, default constructed 0 value is used to verify we
  // only get true empty values from getEmpty()
  for (int i = 1; i < 10000; i++) {
    auto item = conveyor_.getEmpty();
    if (!item) {
      continue;
    }
    EXPECT_EQ(*item, 0);
    *item = i;
    conveyor_.putLoaded(std::move(item));
  }
  // We should be able to expand and contract the conveyor while the thread
  // is still processing:
  conveyor_.expand(1);
  for (int contracted = 0; contracted < 101; contracted += conveyor_.contract(1)) {
  }
  // End the thread loop with a null object:
  // Wait for thread to exit before ending test:
  std::lock_guard<std::mutex> guard(thread_exit_lock_);
  conveyor_.putLoaded(std::unique_ptr<int>());
  thread_exit_.wait(thread_exit_lock_, [this]() -> bool { return !thread_running_; });
}

} // LogTransport
} // Envoy
