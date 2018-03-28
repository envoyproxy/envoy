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

// This Mock logging sink is not implemented using the gmock infrastructure, due to
// apparent limitations in the g++ type system, which fails to compile
// EXPECT_CALL(mock_logger_, log(HasSubstr("substr"))) due to difficulty between
// std::string and absl::string_view. clang compiles it fine.
class MockLogSink : public Logger::SinkDelegate {
public:
  MockLogSink() : Logger::SinkDelegate(Logger::Registry::getSink()) {}
  virtual ~MockLogSink(){};

  // Logger::SinkDelgate
  void log(absl::string_view msg) override { messages_.push_back(std::string(msg)); }
  void flush() override {}

  const std::vector<std::string>& messages() const { return messages_; }

private:
  std::vector<std::string> messages_;
};

} // namespace Envoy
