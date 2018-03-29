#pragma once

#include "envoy/common/time.h"

#include "common/common/logger.h"

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

class MockSystemTimeSource : public SystemTimeSource {
public:
  MockSystemTimeSource();
  ~MockSystemTimeSource();

  MOCK_METHOD0(currentTime, SystemTime());
};

class MockMonotonicTimeSource : public MonotonicTimeSource {
public:
  MockMonotonicTimeSource();
  ~MockMonotonicTimeSource();

  MOCK_METHOD0(currentTime, MonotonicTime());
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
