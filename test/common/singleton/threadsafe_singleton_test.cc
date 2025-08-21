#include <memory>

#include "source/common/common/lock_guard.h"
#include "source/common/common/thread.h"
#include "source/common/singleton/threadsafe_singleton.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class TestSingleton {
public:
  virtual ~TestSingleton() = default;

  virtual void addOne() {
    Thread::LockGuard lock(lock_);
    ++value_;
  }

  virtual int value() {
    Thread::LockGuard lock(lock_);
    return value_;
  }

protected:
  Thread::MutexBasicLockable lock_;
  int value_{0};
};

class EvilMathSingleton : public TestSingleton {
public:
  EvilMathSingleton() { value_ = -50; }
  void addOne() override {
    Thread::LockGuard lock(lock_);
    ++value_;
    ++value_;
  }
};

class AddTen {
public:
  AddTen() {
    thread_ = Thread::threadFactoryForTest().createThread([this]() -> void { threadRoutine(); });
  }
  ~AddTen() {
    thread_->join();
    thread_.reset();
  }

private:
  void threadRoutine() {
    auto& singleton = ThreadSafeSingleton<TestSingleton>::get();
    for (int i = 0; i < 10; ++i) {
      singleton.addOne();
    }
  }
  Thread::ThreadPtr thread_;
};

TEST(ThreadSafeSingleton, BasicCreationAndMutation) {
  auto& singleton = ThreadSafeSingleton<TestSingleton>::get();
  EXPECT_EQ(&singleton, &ThreadSafeSingleton<TestSingleton>::get());
  EXPECT_EQ(0, singleton.value());
  singleton.addOne();
  EXPECT_EQ(1, singleton.value());

  {
    AddTen ten;
    AddTen twenty;
    AddTen thirty;
  }
  EXPECT_EQ(31, singleton.value());
}

TEST(ThreadSafeSingleton, Injection) {
  EvilMathSingleton evil_singleton;

  // Sanity check that other tests didn't cause the main singleton to overflow.
  int latched_value = ThreadSafeSingleton<TestSingleton>::get().value();
  ASSERT_GE(latched_value, 0);

  {
    TestThreadsafeSingletonInjector<TestSingleton> override(&evil_singleton);
    auto& evil_math_reference = ThreadSafeSingleton<TestSingleton>::get();
    EXPECT_NE(latched_value, evil_math_reference.value());
    EXPECT_EQ(-50, evil_math_reference.value());
    evil_math_reference.addOne();
    EXPECT_EQ(-48, evil_math_reference.value());
  }
  EXPECT_EQ(latched_value, ThreadSafeSingleton<TestSingleton>::get().value());
}

} // namespace Envoy
