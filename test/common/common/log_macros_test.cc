#include <iostream>
#include <pthread.h>
#include <string>

#include "common/common/logger.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/logging.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

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

    ENVOY_LOG(critical, "Logger's level: {}",
              ENVOY_LOGGER().level()); // Jinhui Song: check log level
  }

  void logMessageEscapeSequences() { ENVOY_LOG_MISC(info, "line 1 \n line 2 \t tab \\r test"); }

private:
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> stream_;
};

TEST(Logger, All) {
  // This test exists just to ensure all macros compile and run with the expected arguments provided

  TestFilterLog filter;
  filter.logMessage();

  // Misc logging with no facility.
  ENVOY_LOG_MISC(info, "fake message");
}

TEST(Logger, evaluateParams) {
  uint32_t i = 1;

  // Set logger's level to low level.
  // Log message with higher severity and make sure that params were evaluated.
  GET_MISC_LOGGER().set_level(spdlog::level::info);
  ENVOY_LOG_MISC(warn, "test message '{}'", i++);

  ENVOY_LOG_MISC(info, "Test test!"); // Jinhui Song: for test only

  EXPECT_THAT(i, testing::Eq(2));
}

TEST(Logger, doNotEvaluateParams) {
  uint32_t i = 1;

  // Set logger's logging level high and log a message with lower severity
  // params should not be evaluated.
  GET_MISC_LOGGER().set_level(spdlog::level::critical);
  ENVOY_LOG_MISC(error, "test message '{}'", i++);
  EXPECT_THAT(i, testing::Eq(1));
}

TEST(Logger, logAsStatement) {
  // Just log as part of if ... statement
  uint32_t i = 1, j = 1;

  // Set logger's logging level to high
  GET_MISC_LOGGER().set_level(spdlog::level::critical);

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

TEST(Logger, checkLoggerLevel) {
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
 * Test for Fancy Logger macros.
 */
TEST(Fancy, Global) {
  // First log
  EXPECT_THAT(global_epoch__, testing::Eq(nullptr));
  EXPECT_EQ(fancy_log_list__, nullptr);
  FANCY_LOG(info, "Hello world! Here's a line of fancy log!");
  EXPECT_THAT(global_epoch__->load(), testing::Eq(0));
  
  EXPECT_NE(fancy_log_list__, nullptr);
  EXPECT_EQ(fancy_log_list__->file, __FILE__);
  EXPECT_EQ(fancy_log_list__->level, 2);       // info
  
  // Second log
  FANCY_LOG(error, "Fancy Error! Here's the second message!");
  EXPECT_EQ(global_epoch__->load(), 0);
  EXPECT_EQ(fancy_log_list__->file, __FILE__);
  EXPECT_EQ(fancy_log_list__->level, 2);       // file level is default
}

TEST(Fancy, SetLevel) {
  int gepoch = global_epoch__->load();
  const char* file = "P=NP_file";
  setFancyLogLevel(file, spdlog::level::trace);
  EXPECT_EQ(global_epoch__->load(), gepoch + 1);
  EXPECT_EQ(fancy_log_list__->file, file);
  EXPECT_EQ(fancy_log_list__->level, 0);      // trace level

  setFancyLogLevel(__FILE__, spdlog::level::err);
  EXPECT_EQ(fancy_log_list__->file, "P=NP_file");
  EXPECT_EQ(fancy_log_list__->level, 0);
  EXPECT_EQ(fancy_log_list__->next->file, __FILE__);
  EXPECT_EQ(fancy_log_list__->next->level, 4);
  FANCY_LOG(error, "Fancy Error! Here's a test for level.");
  FANCY_LOG(warn, "Warning: you shouldn't see this message!");

}

TEST(Fancy, FastPath) {
  // for loop should expands with the same site?
  printf("You should only see one 'Slow path' in the output..\n");
  setFancyLogLevel(__FILE__, spdlog::level::info);
  for(int i = 0; i < 10; i ++) {
    FANCY_LOG(warn, "Fake warning No. {}", i);
  }
}

void *logThread(void* id) {
  int tid = *static_cast<int*>(id);

  if (tid == 0) {
    FANCY_LOG(info, "Thread {}: thread to set levels", tid);
    setFancyLogLevel(__FILE__, spdlog::level::trace);
    printf(" - level = trace\n");
    setFancyLogLevel(__FILE__, spdlog::level::debug);
    printf(" - level = debug\n");
    setFancyLogLevel(__FILE__, spdlog::level::info);
    printf(" - level = info\n");
    for (int j = 0; j < 10; j ++) {};
    
    setFancyLogLevel(__FILE__, spdlog::level::warn);
    printf(" - level = warn\n");
    setFancyLogLevel(__FILE__, spdlog::level::err);
    printf(" - level = error\n");
    setFancyLogLevel(__FILE__, spdlog::level::critical);
    printf(" - level = critical\n");
  }
  else {
    for (int i = 0; i < 5; i ++) {
      FANCY_LOG(critical, "Thread {} round {}: fake critical log;", tid, i);
      FANCY_LOG(trace, "    fake trace log;");
      FANCY_LOG(debug, "    fake debug log;");
      FANCY_LOG(info, "   fake info;");
      FANCY_LOG(warn, "   fake warn;");
      FANCY_LOG(error, "    fake error;");
      FANCY_LOG(critical, "   fake critical."); 
    }
  }

  pthread_exit(nullptr);
  return nullptr;
}

TEST(FANCY, Threads) {
  // test with multiple threads
  pthread_t threads[2];
  std::vector<int> range = {0, 1, 2};
  for (int id : range) {
    int rc = pthread_create(&threads[id], nullptr, logThread, static_cast<void*>(&range[id]));
    EXPECT_EQ(rc, 0);
  }
  for (int id : range) {
    pthread_join(threads[id], nullptr);
  }
  // pthread_exit(nullptr);
}

// TEST(FANCY, Threads) {
//   // test with multiple threads
//   pthread_t threads[2];
//   int num[] = {0, 1};
//   for (int id : {0, 1}) {
//     int rc = pthread_create(&threads[id], nullptr, logThread, static_cast<void*>(&num[id]));
//     EXPECT_EQ(rc, 0);
//   }
//   for (int id : {0, 1}) {
//     pthread_join(threads[id], nullptr);
//   }
//   pthread_exit(nullptr);
// }


} // namespace Envoy
