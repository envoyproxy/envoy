#pragma once

#include <cstdint>

#include "envoy/common/time.h"
#include "envoy/common/token_bucket.h"
#include "envoy/event/timer.h"

#include "common/common/logger.h"
#include "common/event/real_time_system.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"

namespace Envoy {
/**
 * This action allows us to save a reference parameter to a pointer target.
 */
ACTION_P(SaveArgAddress, target) { *target = &arg0; }

/**
 * Matcher that matches on whether the pointee of both lhs and rhs are equal.
 */
MATCHER_P(PointeesEq, rhs, "") {
  *result_listener << testing::PrintToString(*arg) + " != " + testing::PrintToString(*rhs);
  return *arg == *rhs;
}

/**
 * Simple mock that just lets us make sure a method gets called or not called form a lambda.
 */
class ReadyWatcher {
public:
  ReadyWatcher();
  ~ReadyWatcher();

  MOCK_METHOD0(ready, void());
};

class MockTimeSource : public TimeSource {
public:
  MockTimeSource();
  ~MockTimeSource();

  MOCK_METHOD0(systemTime, SystemTime());
  MOCK_METHOD0(monotonicTime, MonotonicTime());
};

class MockTimeSystem : public Event::TimeSystem {
public:
  MockTimeSystem();
  ~MockTimeSystem();

  // TODO(#4160): Eliminate all uses of MockTimeSystem, replacing with SimulatedTimeSystem,
  // where timer callbacks are triggered by the advancement of time. This implementation
  // matches recent behavior, where real-time timers were created directly in libevent
  // by dispatcher_impl.cc.
  Event::SchedulerPtr createScheduler(Event::Libevent::BasePtr& base) override {
    return real_time_system_.createScheduler(base);
  }
  MOCK_METHOD0(systemTime, SystemTime());
  MOCK_METHOD0(monotonicTime, MonotonicTime());

  Event::RealTimeSystem real_time_system_;
};

class MockTokenBucket : public TokenBucket {
public:
  MockTokenBucket();
  ~MockTokenBucket();

  MOCK_METHOD1(consume, bool(uint64_t));
};

// Captures absl::string_view parameters into temp strings, for use
// with gmock's SaveArg<n>. Providing an absl::string_view compiles,
// but fails because by the time you examine the saved value, its
// backing store will go out of scope.
class StringViewSaver {
public:
  void operator=(absl::string_view view) { value_ = std::string(view); }
  const std::string& value() const { return value_; }
  operator std::string() const { return value_; }

private:
  std::string value_;
};

inline bool operator==(const char* str, const StringViewSaver& saver) {
  return saver.value() == str;
}

inline bool operator==(const StringViewSaver& saver, const char* str) {
  return saver.value() == str;
}

} // namespace Envoy
