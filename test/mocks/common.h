#pragma once

#include <cstdint>

#include "envoy/common/backoff_strategy.h"
#include "envoy/common/conn_pool.h"
#include "envoy/common/key_value_store.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/common/time.h"

#include "source/common/common/logger.h"

#include "test/test_common/test_time.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

  MOCK_METHOD(void, ready, ());
};

// TODO(jmarantz): get rid of this and use SimulatedTimeSystem in its place.
class MockTimeSystem : public Event::TestTimeSystem {
public:
  MockTimeSystem();
  ~MockTimeSystem() override;

  // TODO(#4160): Eliminate all uses of MockTimeSystem, replacing with SimulatedTimeSystem,
  // where timer callbacks are triggered by the advancement of time. This implementation
  // matches recent behavior, where real-time timers were created directly in libevent
  // by dispatcher_impl.cc.
  Event::SchedulerPtr createScheduler(Event::Scheduler& base_scheduler,
                                      Event::CallbackScheduler& cb_scheduler) override {
    return real_time_.createScheduler(base_scheduler, cb_scheduler);
  }
  void advanceTimeWaitImpl(const Duration& duration) override {
    real_time_.advanceTimeWaitImpl(duration);
  }
  void advanceTimeAsyncImpl(const Duration& duration) override {
    real_time_.advanceTimeAsyncImpl(duration);
  }
  MOCK_METHOD(SystemTime, systemTime, ());
  MOCK_METHOD(MonotonicTime, monotonicTime, ());

  Event::TestRealTimeSystem real_time_; // NO_CHECK_FORMAT(real_time)
};

class MockBackOffStrategy : public BackOffStrategy {
public:
  ~MockBackOffStrategy() override = default;

  MOCK_METHOD(uint64_t, nextBackOffMs, ());
  MOCK_METHOD(void, reset, ());
  MOCK_METHOD(void, reset, (uint64_t base_interval));
  MOCK_METHOD(bool, isOverTimeLimit, (uint64_t interval_ms), (const));
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

class MockScopeTrackedObject : public ScopeTrackedObject {
public:
  MOCK_METHOD(void, dumpState, (std::ostream&, int), (const));
  MOCK_METHOD(ExecutionContext*, executionContext, (), (const));
};

namespace ConnectionPool {

class MockCancellable : public Cancellable {
public:
  MockCancellable();
  ~MockCancellable() override;

  // ConnectionPool::Cancellable
  MOCK_METHOD(void, cancel, (CancelPolicy cancel_policy));
};
} // namespace ConnectionPool

namespace Random {
class MockRandomGenerator : public RandomGenerator {
public:
  MockRandomGenerator();
  MockRandomGenerator(uint64_t value);
  ~MockRandomGenerator() override;

  MOCK_METHOD(uint64_t, random, ());
  MOCK_METHOD(std::string, uuid, ());

  uint64_t value_;
  const std::string uuid_{"a121e9e1-feae-4136-9e0e-6fac343d56c9"};
};

} // namespace Random

class MockKeyValueStore : public KeyValueStore {
public:
  MOCK_METHOD(void, addOrUpdate,
              (absl::string_view, absl::string_view, absl::optional<std::chrono::seconds> ttl));
  MOCK_METHOD(void, remove, (absl::string_view));
  MOCK_METHOD(absl::optional<absl::string_view>, get, (absl::string_view));
  MOCK_METHOD(void, flush, ());
  MOCK_METHOD(void, iterate, (ConstIterateCb), (const));
};

class MockKeyValueStoreFactory : public KeyValueStoreFactory {
public:
  MockKeyValueStoreFactory();
  MOCK_METHOD(KeyValueStorePtr, createStore,
              (const Protobuf::Message&, ProtobufMessage::ValidationVisitor&, Event::Dispatcher&,
               Filesystem::Instance&));
  MOCK_METHOD(ProtobufTypes::MessagePtr, createEmptyConfigProto, ());
  std::string category() const override { return "envoy.common.key_value"; }
  std::string name() const override { return "mock_key_value_store_factory"; }
};

struct MockLogSink : Logger::SinkDelegate {
  MockLogSink(Logger::DelegatingLogSinkSharedPtr log_sink) : Logger::SinkDelegate(log_sink) {
    setDelegate();
  }
  ~MockLogSink() override { restoreDelegate(); }

  MOCK_METHOD(void, log, (absl::string_view, const spdlog::details::log_msg&));
  MOCK_METHOD(void, logWithStableName,
              (absl::string_view, absl::string_view, absl::string_view, absl::string_view));
  void flush() override {}
};

} // namespace Envoy
