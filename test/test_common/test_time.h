#pragma once

#include "common/event/real_time_system.h"

#include "test/mocks/common.h"
#include "test/test_common/global.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time_system.h"

namespace Envoy {
namespace Event {

class TestRealTimeSystem : public TestTimeSystem {
public:
  // TestTimeSystem
  void sleep(const Duration& duration) override;
  Thread::CondVar::WaitStatus
  waitFor(Thread::MutexBasicLockable& mutex, Thread::CondVar& condvar,
          const Duration& duration) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) override;

  // Event::TimeSystem
  Event::SchedulerPtr createScheduler(Event::Libevent::BasePtr& libevent) override {
    return real_time_system_.createScheduler(libevent);
  }

  // TimeSource
  SystemTime systemTime() override;
  MonotonicTime monotonicTime() override;

private:
  Event::RealTimeSystem real_time_system_;
};

// TODO(jmarantz): get rid of this and use SimulatedTimeSystem in its place.
class MockTimeSystem : public Event::TestTimeSystem {
public:
  MockTimeSystem();
  ~MockTimeSystem();

  // TODO(#4160): Eliminate all uses of MockTimeSystem, replacing with SimulatedTimeSystem,
  // where timer callbacks are triggered by the advancement of time. This implementation
  // matches recent behavior, where real-time timers were created directly in libevent
  // by dispatcher_impl.cc.
  Event::SchedulerPtr createScheduler(Event::Libevent::BasePtr& base) override;
  void sleep(const Duration& duration) override;
  Thread::CondVar::WaitStatus
  waitFor(Thread::MutexBasicLockable& mutex, Thread::CondVar& condvar,
          const Duration& duration) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) override;
  MOCK_METHOD0(systemTime, SystemTime());
  MOCK_METHOD0(monotonicTime, MonotonicTime());

  TestRealTimeSystem real_time_system_; // NO_CHECK_FORMAT(real_time)
};

// Ensures that only one type of time-system is instantiated at a time.
class SingletonTimeSystemHelper {
public:
  explicit SingletonTimeSystemHelper() : time_system_(nullptr) {}
  void set(TestTimeSystem* time_system) {
    if (time_system_ == nullptr) {
      time_system_ = time_system;
    } else {
      ASSERT(time_system == time_system_);
    }
  }

  TestTimeSystem* lazyInit() {
    if (time_system_ == nullptr) {
      default_time_system_ = std::make_unique<Test::Global<TestRealTimeSystem>>();
      time_system_ = &(default_time_system_->get());
    }
    return time_system_;
  }

  TestTimeSystem& operator*() { return *lazyInit(); }
  TestTimeSystem* operator->() { return lazyInit(); }

private:
  TestTimeSystem* time_system_;
  std::unique_ptr<Test::Global<TestRealTimeSystem>> default_time_system_;
};
using GlobalTimeSystem = Test::Global<SingletonTimeSystemHelper>;

template <class Type> class TestTime {
public:
  TestTime() { global_time_system_->set(&(time_system_.get())); }
  Type& operator*() { return *time_system_; }
  Type* operator->() { return &(time_system_.get()); }

private:
  Test::Global<Type> time_system_;
  GlobalTimeSystem global_time_system_;
};

} // namespace Event

// Instantiates real-time sources for testing purposes. In general, this is a
// bad idea, and tests should use simulated or mock time.
//
// TODO(#4160): change all references to this class to instantiate instead to
// some kind of mock or simulated-time source.
class DangerousDeprecatedTestTime {
public:
  DangerousDeprecatedTestTime();

  Event::TestTimeSystem& timeSystem() { return time_system_; }

private:
  Event::TestRealTimeSystem time_system_;
};

} // namespace Envoy
