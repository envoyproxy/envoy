#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include "source/common/common/fine_grain_logger.h"
#include "source/common/common/logger.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/logging.h"

#include "absl/synchronization/barrier.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

using namespace std::chrono_literals;
using ::testing::HasSubstr;

class TestFilterLog : public Logger::Loggable<Logger::Id::filter> {
public:
  void logMessage() {
    ENVOY_LOG(trace, "fake message");
    ENVOY_LOG(debug, "fake message");
    ENVOY_LOG(warn, "fake message");
    ENVOY_LOG(error, "fake message");
    ENVOY_LOG(critical, "fake message");
    ENVOY_CONN_LOG(info, "fake message", connection_);
    ENVOY_STREAM_LOG(info, "fake message", stream_);
    ENVOY_CONN_LOG(error, "fake error", connection_);
    ENVOY_STREAM_LOG(error, "fake error", stream_);
    ENVOY_TAGGED_LOG(info, tags_, "fake message {}", "val");
    ENVOY_TAGGED_LOG(info, (std::map<std::string, std::string>{{"key", "val"}}), "fake message {}",
                     "val");
    ENVOY_TAGGED_CONN_LOG(info, tags_, connection_, "fake message {}", "val");
    ENVOY_TAGGED_CONN_LOG(info, (std::map<std::string, std::string>{{"key", "val"}}), connection_,
                          "fake message {}", "val");
    ENVOY_TAGGED_STREAM_LOG(info, tags_, stream_, "fake message {}", "val");
    ENVOY_TAGGED_STREAM_LOG(info, (std::map<std::string, std::string>{{"key", "val"}}), stream_,
                            "fake message {}", "val");
  }

  void logConnTraceMessage() { ENVOY_CONN_LOG(trace, "fake trace message", connection_); }

  void logStreamTraceMessage() { ENVOY_STREAM_LOG(trace, "fake trace message", stream_); }

  void logEventTraceMessage() {
    ENVOY_CONN_LOG_EVENT(trace, "fake_event", "fake message", connection_);
  }

  void logMessageEscapeSequences() { ENVOY_LOG_MISC(info, "line 1 \n line 2 \t tab \\r test"); }

private:
  std::map<std::string, std::string> tags_{{"key", "val"}};
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> stream_;
};

TEST(Logger, StreamFineGrainLoggerRegistration) {
  Envoy::Thread::MutexBasicLockable lock;
  Logger::Context logging_context{spdlog::level::warn, Logger::Context::getFineGrainLogFormat(),
                                  lock, false};
  Logger::Context::enableFineGrainLogger();
  TestFilterLog filter;
  getFineGrainLogContext().removeFineGrainLogEntryForTest(__FILE__);

  // Make sure fine-grain logger is initialized even the log level is trace.
  filter.logStreamTraceMessage();
  filter.logStreamTraceMessage();
  filter.logStreamTraceMessage();
  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(__FILE__);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(Logger, EventFineGrainLoggerRegistration) {
  Envoy::Thread::MutexBasicLockable lock;
  Logger::Context logging_context{spdlog::level::warn, Logger::Context::getFineGrainLogFormat(),
                                  lock, false};
  Logger::Context::enableFineGrainLogger();
  TestFilterLog filter;
  getFineGrainLogContext().removeFineGrainLogEntryForTest(__FILE__);

  // Make sure fine-grain logger is initialized even the log level is trace.
  filter.logEventTraceMessage();
  filter.logEventTraceMessage();
  filter.logEventTraceMessage();
  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(__FILE__);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(Logger, ConnFineGrainLoggerRegistration) {
  Envoy::Thread::MutexBasicLockable lock;
  Logger::Context logging_context{spdlog::level::warn, Logger::Context::getFineGrainLogFormat(),
                                  lock, false};
  Logger::Context::enableFineGrainLogger();
  TestFilterLog filter;
  getFineGrainLogContext().removeFineGrainLogEntryForTest(__FILE__);

  // Make sure fine-grain logger is initialized even the log level is trace.
  filter.logConnTraceMessage();
  filter.logConnTraceMessage();
  filter.logConnTraceMessage();
  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(__FILE__);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(Logger, All) {
  // This test exists just to ensure all macros compile and run with the expected arguments provided

  TestFilterLog filter;
  filter.logMessage();

  // Misc logging with no facility.
  ENVOY_LOG_MISC(info, "fake message");
}

TEST(Logger, EvaluateParams) {
  uint32_t i = 1;

  // Set logger's level to low level.
  // Log message with higher severity and make sure that params were evaluated.
  LogLevelSetter save_levels(spdlog::level::info);
  ENVOY_LOG_MISC(warn, "test message '{}'", i++);

  EXPECT_THAT(i, testing::Eq(2));
}

TEST(Logger, DoNotEvaluateParams) {
  uint32_t i = 1;

  // Set logger's logging level high and log a message with lower severity
  // params should not be evaluated.
  LogLevelSetter save_levels(spdlog::level::critical);
  ENVOY_LOG_MISC(error, "test message '{}'", i++);
  EXPECT_THAT(i, testing::Eq(1));
}

TEST(Logger, LogAsStatement) {
  // Just log as part of if ... statement
  uint32_t i = 1, j = 1;

  // Set logger's logging level to high
  LogLevelSetter save_levels(spdlog::level::critical);

  // Make sure that if statement inside of LOGGER macro does not catch trailing
  // else ....
  if (true) // NOLINT(readability-braces-around-statements)
    ENVOY_LOG_MISC(warn, "test message 1 '{}'", i++);
  else // NOLINT(readability-braces-around-statements)
    ENVOY_LOG_MISC(critical, "test message 2 '{}'", j++);

  EXPECT_THAT(i, testing::Eq(1));
  EXPECT_THAT(j, testing::Eq(1));

  // Do the same with curly brackets
  if (true) {
    ENVOY_LOG_MISC(warn, "test message 3 '{}'", i++);
  } else {
    ENVOY_LOG_MISC(critical, "test message 4 '{}'", j++);
  }

  EXPECT_THAT(i, testing::Eq(1));
  EXPECT_THAT(j, testing::Eq(1));
}

TEST(Logger, CheckLoggerLevel) {
  class LogTestClass : public Logger::Loggable<Logger::Id::misc> {
  public:
    void setLevel(const spdlog::level::level_enum level) { ENVOY_LOGGER().set_level(level); }
    uint32_t executeAtTraceLevel() {
      if (ENVOY_LOG_CHECK_LEVEL(trace)) {
        //  Logger's level was at least trace
        return 1;
      } else {
        // Logger's level was higher than trace
        return 2;
      };
    }
  };

  LogTestClass test_obj;

  // Set Loggers severity low
  test_obj.setLevel(spdlog::level::trace);
  EXPECT_THAT(test_obj.executeAtTraceLevel(), testing::Eq(1));

  test_obj.setLevel(spdlog::level::info);
  EXPECT_THAT(test_obj.executeAtTraceLevel(), testing::Eq(2));
}

void spamCall(std::function<void()>&& call_to_spam, const uint32_t num_threads) {
  std::vector<std::thread> threads(num_threads);
  auto barrier = std::make_unique<absl::Barrier>(num_threads);

  for (auto& thread : threads) {
    thread = std::thread([&call_to_spam, &barrier] {
      // Allow threads to accrue, to maximize concurrency on the call we are testing.
      if (barrier->Block()) {
        barrier.reset();
      }
      call_to_spam();
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
}

class SparseLogMacrosTest : public testing::TestWithParam<bool>,
                            public Logger::Loggable<Logger::Id::filter> {
public:
  SparseLogMacrosTest() : use_misc_macros_(GetParam()) { evaluations() = 0; }

  void logSomething() {
    if (use_misc_macros_) {
      ENVOY_LOG_ONCE_MISC(error, "foo1 '{}'", evaluations()++);
    } else {
      ENVOY_LOG_ONCE(error, "foo1 '{}'", evaluations()++);
    }
  }

  void logSomethingElse() {
    if (use_misc_macros_) {
      ENVOY_LOG_ONCE_MISC(error, "foo2 '{}'", evaluations()++);
    } else {
      ENVOY_LOG_ONCE(error, "foo2 '{}'", evaluations()++);
    }
  }

  void logSomethingBelowLogLevelOnce() {
    if (use_misc_macros_) {
      ENVOY_LOG_ONCE_MISC(debug, "foo3 '{}'", evaluations()++);
    } else {
      ENVOY_LOG_ONCE(debug, "foo3 '{}'", evaluations()++);
    }
  }

  void logSomethingThrice() {
    if (use_misc_macros_) {
      ENVOY_LOG_FIRST_N_MISC(error, 3, "foo4 '{}'", evaluations()++);
    } else {
      ENVOY_LOG_FIRST_N(error, 3, "foo4 '{}'", evaluations()++);
    }
  }

  void logEverySeventh() {
    if (use_misc_macros_) {
      ENVOY_LOG_EVERY_NTH_MISC(error, 7, "foo5 '{}'", evaluations()++);
    } else {
      ENVOY_LOG_EVERY_NTH(error, 7, "foo5 '{}'", evaluations()++);
    }
  }

  void logEveryPow2() {
    if (use_misc_macros_) {
      ENVOY_LOG_EVERY_POW_2_MISC(error, "foo6 '{}'", evaluations()++);
    } else {
      ENVOY_LOG_EVERY_POW_2(error, "foo6 '{}'", evaluations()++);
    }
  }

  void logEverySecond() {
    if (use_misc_macros_) {
      ENVOY_LOG_PERIODIC_MISC(error, 1s, "foo7 '{}'", evaluations()++);
    } else {
      ENVOY_LOG_PERIODIC(error, 1s, "foo7 '{}'", evaluations()++);
    }
  }

  void logOnceIf(bool condition) {
    if (use_misc_macros_) {
      ENVOY_LOG_ONCE_MISC_IF(error, condition, "foo8 '{}'", evaluations()++);
    } else {
      ENVOY_LOG_ONCE_IF(error, condition, "foo8 '{}'", evaluations()++);
    }
  }

  void logSomethingThriceIf(bool condition) {
    if (use_misc_macros_) {
      ENVOY_LOG_FIRST_N_MISC_IF(error, 3, condition, "foo9 '{}'", evaluations()++);
    } else {
      ENVOY_LOG_FIRST_N_IF(error, 3, condition, "foo9 '{}'", evaluations()++);
    }
  }

  std::atomic<int32_t>& evaluations() { MUTABLE_CONSTRUCT_ON_FIRST_USE(std::atomic<int32_t>); };

  const bool use_misc_macros_;
  LogLevelSetter save_levels_{spdlog::level::info};
};

INSTANTIATE_TEST_SUITE_P(MiscOrNot, SparseLogMacrosTest, testing::Values(false, true));

TEST_P(SparseLogMacrosTest, All) {
  constexpr uint32_t kNumThreads = 100;
  spamCall(
      [this]() {
        logSomething();
        logSomething();
      },
      kNumThreads);
  EXPECT_EQ(1, evaluations());
  spamCall(
      [this]() {
        logSomethingElse();
        logSomethingElse();
      },
      kNumThreads);
  // Two distinct log lines ought to result in two evaluations, and no more.
  EXPECT_EQ(2, evaluations());

  spamCall([this]() { logSomethingThrice(); }, kNumThreads);
  // Single log line should be emitted 3 times.
  EXPECT_EQ(5, evaluations());

  spamCall([this]() { logEverySeventh(); }, kNumThreads);
  // (100 threads / log every 7th) + 1s = 15 more evaluations upon logging very 7th.
  EXPECT_EQ(20, evaluations());

  logEveryPow2();
  // First call ought to propagate.
  EXPECT_EQ(21, evaluations());

  spamCall([this]() { logEveryPow2(); }, kNumThreads);
  // 64 is the highest power of two that fits when kNumThreads == 100.
  // We should log on 2, 4, 8, 16, 32, 64, which means we can expect to add 6 more evaluations.
  EXPECT_EQ(27, evaluations());

  spamCall([this]() { logEverySecond(); }, kNumThreads);
  // First call ought to evaluate.
  EXPECT_EQ(28, evaluations());

  // We expect one log entry / second. Therefore each spamCall ought to result in one
  // more evaluation. This depends on real time and not sim time, hopefully 1 second
  // is enough to not introduce flakes in practice.
  std::this_thread::sleep_for(1s); // NOLINT
  spamCall([this]() { logEverySecond(); }, kNumThreads);
  EXPECT_EQ(29, evaluations());

  spamCall([this]() { logSomethingBelowLogLevelOnce(); }, kNumThreads);
  // We shouldn't observe additional argument evaluations for log lines below the configured
  // log level.
  EXPECT_EQ(29, evaluations());

  spamCall([this]() { logSomethingThriceIf(false); }, kNumThreads);
  // As the condition was false the logs didn't evaluate.
  EXPECT_EQ(29, evaluations());
  spamCall([this]() { logOnceIf(false); }, kNumThreads);
  // As the condition was false the logs didn't evaluate.
  EXPECT_EQ(29, evaluations());

  spamCall([this]() { logOnceIf(true); }, kNumThreads);
  // First call evaluates.
  EXPECT_EQ(30, evaluations());

  // First and last call evaluates as the condition holds.
  logSomethingThriceIf(true);
  logSomethingThriceIf(false);
  logSomethingThriceIf(true);
  EXPECT_EQ(32, evaluations());
  spamCall([this]() { logSomethingThriceIf(true); }, kNumThreads);
  // Only one remaining log was left.
  EXPECT_EQ(33, evaluations());
}

TEST(RegistryTest, LoggerWithName) {
  EXPECT_EQ(nullptr, Logger::Registry::logger("blah"));
  EXPECT_EQ("upstream", Logger::Registry::logger("upstream")->name());
}

class FormatTest : public testing::Test {
public:
  static void logMessageEscapeSequences() {
    ENVOY_LOG_MISC(info, "line 1 \n line 2 \t tab \\r test");
  }
};

TEST_F(FormatTest, OutputUnescaped) {
  const Envoy::ExpectedLogMessages message{{"info", "line 1 \n line 2 \t tab \\r test"}};
  EXPECT_LOG_CONTAINS_ALL_OF(message, logMessageEscapeSequences());
}

TEST_F(FormatTest, OutputEscaped) {
  // Note this uses a raw string literal
  const Envoy::ExpectedLogMessages message{{"info", R"(line 1 \n line 2 \t tab \\r test)"}};
  EXPECT_LOG_CONTAINS_ALL_OF_ESCAPED(message, logMessageEscapeSequences());
}

/**
 * Test for Fine-Grain Logger convenient macros.
 */
TEST(FineGrainLog, Global) {
  FINE_GRAIN_LOG(info, "Hello world! Here's a line of fine-grain log!");
  FINE_GRAIN_LOG(error, "FineGrainLog Error! Here's the second message!");

  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> stream_;
  FINE_GRAIN_CONN_LOG(warn, "Fake info {} of connection", connection_, 1);
  FINE_GRAIN_STREAM_LOG(warn, "Fake warning {} of stream", stream_, 1);

  FINE_GRAIN_LOG(critical, "Critical message for later flush.");
  FINE_GRAIN_FLUSH_LOG();
}

TEST(FineGrainLog, FastPath) {
  getFineGrainLogContext().setFineGrainLogger(__FILE__, spdlog::level::info);
  for (int i = 0; i < 10; i++) {
    FINE_GRAIN_LOG(warn, "Fake warning No. {}", i);
  }
}

TEST(FineGrainLog, SetLevel) {
  const char* file = "P=NP_file";
  bool res = getFineGrainLogContext().setFineGrainLogger(file, spdlog::level::trace);
  EXPECT_EQ(res, false);
  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(file);
  EXPECT_EQ(p, nullptr);

  res = getFineGrainLogContext().setFineGrainLogger(__FILE__, spdlog::level::err);
  EXPECT_EQ(res, true);
  FINE_GRAIN_LOG(error, "FineGrainLog Error! Here's a test for level.");
  FINE_GRAIN_LOG(warn, "Warning: you shouldn't see this message!");
  p = getFineGrainLogContext().getFineGrainLogEntry(__FILE__);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::err);

  getFineGrainLogContext().setAllFineGrainLoggers(spdlog::level::info);
  FINE_GRAIN_LOG(info, "Info: all loggers back to info.");
  FINE_GRAIN_LOG(debug, "Debug: you shouldn't see this message!");
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(__FILE__)->level(), spdlog::level::info);
}

TEST(FineGrainLog, Iteration) {
  FINE_GRAIN_LOG(info, "Info: iteration test begins.");
  getFineGrainLogContext().setAllFineGrainLoggers(spdlog::level::info);
  std::string output = getFineGrainLogContext().listFineGrainLoggers();
  EXPECT_THAT(output, HasSubstr("  " __FILE__ ": info"));
  getFineGrainLogContext().setFineGrainLogger(__FILE__, spdlog::level::err);

  FINE_GRAIN_LOG(warn, "Warning: now level is warning, format changed (Date removed).");
  FINE_GRAIN_LOG(warn, getFineGrainLogContext().listFineGrainLoggers());
}

TEST(FineGrainLog, ListIteration) {
  FINE_GRAIN_LOG(info, "Info: iteration test begins.");
  const absl::flat_hash_map<spdlog::level::level_enum, std::string> log_level_strings = {
      {spdlog::level::trace, "trace"}, {spdlog::level::debug, "debug"},
      {spdlog::level::info, "info"},   {spdlog::level::warn, "warn"},
      {spdlog::level::err, "error"},   {spdlog::level::critical, "critical"},
      {spdlog::level::off, "off"},
  };

  std::string output;
  for (const auto& [level, level_str] : log_level_strings) {
    getFineGrainLogContext().setAllFineGrainLoggers(level);
    output = getFineGrainLogContext().listFineGrainLoggers();
    EXPECT_THAT(output, HasSubstr("  " __FILE__ ": " + level_str));
  }
}

TEST(FineGrainLog, Context) {
  FINE_GRAIN_LOG(info, "Info: context API needs test.");
  bool enable_fine_grain_logging = Logger::Context::useFineGrainLogger();
  printf(" --> If use fine-grain logger: %d\n", enable_fine_grain_logging);
  if (enable_fine_grain_logging) {
    FINE_GRAIN_LOG(critical, "Cmd option set: all previous Envoy Log should be converted now!");
  }
  Logger::Context::enableFineGrainLogger();
  EXPECT_EQ(Logger::Context::useFineGrainLogger(), true);
  EXPECT_EQ(Logger::Context::getFineGrainLogFormat(), "[%Y-%m-%d %T.%e][%t][%l] [%g:%#] %v");
}

} // namespace Envoy
