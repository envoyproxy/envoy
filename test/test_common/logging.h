#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "source/common/common/logger.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/mutex.h"
#include "spdlog/spdlog.h"

namespace Envoy {

/**
 * Provides a mechanism to temporarily set the logging level on
 * construction, restoring its previous state on destruction.
 *
 * The log_level is the minimum log severity required to print messages.
 * Messages below this loglevel will be suppressed.
 *
 * Note that during the scope of this object, command-line overrides, e.g.,
 * --log-level trace, will not take effect.
 *
 * Also note: instantiating this setter should only occur when the system is
 * in a quiescent state, e.g. at startup or between tests.
 *
 * This is intended for use in EXPECT_LOG_CONTAINS and similar macros.
 */
class LogLevelSetter {
public:
  explicit LogLevelSetter(spdlog::level::level_enum log_level);
  ~LogLevelSetter();

private:
  std::vector<spdlog::level::level_enum> previous_levels_;
  FineGrainLogLevelMap previous_fine_grain_levels_;
};

/**
 * Records log messages in a vector<string>, forwarding them to the previous
 * delegate. This is useful for unit-testing log messages while still being able
 * to see them on stderr.
 *
 * Also note: instantiating this sink should only occur when the system is
 * in a quiescent state, e.g. at startup or between tests
 *
 * This is intended for use in EXPECT_LOG_CONTAINS and similar macros.
 */
class LogRecordingSink : public Logger::SinkDelegate {
public:
  explicit LogRecordingSink(Logger::DelegatingLogSinkSharedPtr log_sink);
  ~LogRecordingSink() override;

  // Logger::SinkDelegate
  void log(absl::string_view msg, const spdlog::details::log_msg& log_msg) override;
  void flush() override;

  const std::vector<std::string> messages() const {
    absl::MutexLock ml(&mtx_);
    std::vector<std::string> copy(messages_);
    return copy;
  }

private:
  mutable absl::Mutex mtx_;
  std::vector<std::string> messages_ ABSL_GUARDED_BY(mtx_);
};

using StringPair = std::pair<std::string, std::string>;

using ExpectedLogMessages = std::vector<StringPair>;

// Below macros specify Envoy:: before class names so that the macro can be used outside of
// namespace Envoy.

// Alias for EXPECT_LOG_CONTAINS_ALL_OF_HELPER, with escaped=true
#define EXPECT_LOG_CONTAINS_ALL_OF_ESCAPED(expected_messages, stmt)                                \
  EXPECT_LOG_CONTAINS_ALL_OF_HELPER(expected_messages, stmt, true)

// Alias for EXPECT_LOG_CONTAINS_ALL_OF_HELPER, with escaped=false
#define EXPECT_LOG_CONTAINS_ALL_OF(expected_messages, stmt)                                        \
  EXPECT_LOG_CONTAINS_ALL_OF_HELPER(expected_messages, stmt, false)

// Validates that when stmt is executed, log messages containing substr and loglevel will be
// emitted. Escaped=true sets the behavior to function like the --log-format-escaped CLI flag.
// Failure message e.g.,
//
// Logs:
//  [2018-04-12 05:51:00.245][7290192][debug][upstream] grpc_mux_impl.cc:160] Received gRPC
//  [2018-04-12 05:51:00.246][7290192][warning][upstream] grpc_mux_impl.cc:63] Called bar
//  [2018-04-12 05:51:00.246][7290192][trace][upstream] grpc_mux_impl.cc:80] Sending foo
//  Does NOT contain:
//    'warning', 'Too many sendDiscoveryRequest calls for baz’
//    'warning', 'Too man sendDiscoveryRequest calls for foo'
#define EXPECT_LOG_CONTAINS_ALL_OF_HELPER(expected_messages, stmt, escaped)                        \
  do {                                                                                             \
    ASSERT_FALSE(expected_messages.empty()) << "Expected messages cannot be empty.";               \
    Envoy::LogLevelSetter save_levels(spdlog::level::trace);                                       \
    Envoy::Logger::DelegatingLogSinkSharedPtr sink_ptr = Envoy::Logger::Registry::getSink();       \
    sink_ptr->setShouldEscape(escaped);                                                            \
    Envoy::LogRecordingSink log_recorder(sink_ptr);                                                \
    stmt;                                                                                          \
    auto messages = log_recorder.messages();                                                       \
    if (messages.empty()) {                                                                        \
      FAIL() << "Expected message(s), but NONE was recorded.";                                     \
    }                                                                                              \
    Envoy::ExpectedLogMessages failed_expectations;                                                \
    for (const Envoy::StringPair& expected : expected_messages) {                                  \
      const auto log_message =                                                                     \
          std::find_if(messages.begin(), messages.end(), [&expected](const std::string& message) { \
            return (message.find(expected.second) != std::string::npos) &&                         \
                   (message.find(expected.first) != std::string::npos);                            \
          });                                                                                      \
      if (log_message == messages.end()) {                                                         \
        failed_expectations.push_back(expected);                                                   \
      }                                                                                            \
    }                                                                                              \
    if (!failed_expectations.empty()) {                                                            \
      std::string failed_message;                                                                  \
      absl::StrAppend(&failed_message, "\nLogs:\n ", absl::StrJoin(messages, " "),                 \
                      "\n Do NOT contain:\n");                                                     \
      for (const auto& expectation : failed_expectations) {                                        \
        absl::StrAppend(&failed_message, "  '", expectation.first, "', '", expectation.second,     \
                        "'\n");                                                                    \
      }                                                                                            \
      FAIL() << failed_message;                                                                    \
    }                                                                                              \
  } while (false)

// Validates that when stmt is executed, log message containing substr and loglevel will NOT be
// emitted. Failure message e.g.,
//
// Logs:
//  [2018-04-12 05:51:00.245][7290192][warning][upstream] grpc_mux_impl.cc:160] Received gRPC
//  [2018-04-12 05:51:00.246][7290192][trace][upstream] grpc_mux_impl.cc:63] Called bar
//  Should NOT contain:
//   'warning', 'Received gRPC’
#define EXPECT_LOG_NOT_CONTAINS(loglevel, substr, stmt)                                            \
  do {                                                                                             \
    Envoy::LogLevelSetter save_levels(spdlog::level::trace);                                       \
    Envoy::LogRecordingSink log_recorder(Envoy::Logger::Registry::getSink());                      \
    stmt;                                                                                          \
    auto messages = log_recorder.messages();                                                       \
    for (const std::string& message : messages) {                                                  \
      if ((message.find(substr) != std::string::npos) &&                                           \
          (message.find(loglevel) != std::string::npos)) {                                         \
        FAIL() << "\nLogs:\n " << absl::StrJoin(messages, " ") << "\n Should NOT contain:\n '"     \
               << loglevel << "', '" << substr "'\n";                                              \
      }                                                                                            \
    }                                                                                              \
  } while (false)

// Validates that when stmt is executed, the supplied substring matches at least one log message.
// Failure message e.g.,
//
// Logs:
//  [2018-04-12 05:51:00.245][7290192][debug][upstream] grpc_mux_impl.cc:160] Received gRPC
//  [2018-04-12 05:51:00.246][7290192][trace][upstream] grpc_mux_impl.cc:80] Sending foo
//  Do NOT contain:
//    'warning', 'Too many sendDiscoveryRequest calls for baz’
#define EXPECT_LOG_CONTAINS(loglevel, substr, stmt)                                                \
  do {                                                                                             \
    const Envoy::ExpectedLogMessages message{{loglevel, substr}};                                  \
    EXPECT_LOG_CONTAINS_ALL_OF(message, stmt);                                                     \
  } while (false)

// Validates that when stmt is executed, the supplied substring occurs exactly the specified
// number of times.
#define EXPECT_LOG_CONTAINS_N_TIMES(loglevel, substr, expected_occurrences, stmt)                  \
  do {                                                                                             \
    Envoy::LogLevelSetter save_levels(spdlog::level::trace);                                       \
    Envoy::LogRecordingSink log_recorder(Envoy::Logger::Registry::getSink());                      \
    stmt;                                                                                          \
    auto messages = log_recorder.messages();                                                       \
    uint64_t actual_occurrences = 0;                                                               \
    for (const std::string& message : messages) {                                                  \
      if ((message.find(substr) != std::string::npos) &&                                           \
          (message.find(loglevel) != std::string::npos)) {                                         \
        actual_occurrences++;                                                                      \
      }                                                                                            \
    }                                                                                              \
    EXPECT_EQ(expected_occurrences, actual_occurrences);                                           \
  } while (false)

// Validates that when stmt is executed, no logs will be emitted.
// Expected equality of these values:
//   0
//   logs.size()
//     Which is: 3
//  Logs:
//   [2018-04-12 05:51:00.245][7290192][debug][upstream] grpc_mux_impl.cc:160] Received gRPC
//   [2018-04-12 05:51:00.246][7290192][trace][upstream] grpc_mux_impl.cc:80] Sending foo
#define EXPECT_NO_LOGS(stmt)                                                                       \
  do {                                                                                             \
    Envoy::LogLevelSetter save_levels(spdlog::level::trace);                                       \
    Envoy::LogRecordingSink log_recorder(Envoy::Logger::Registry::getSink());                      \
    stmt;                                                                                          \
    const std::vector<std::string> logs = log_recorder.messages();                                 \
    ASSERT_EQ(0, logs.size()) << " Logs:\n   " << absl::StrJoin(logs, "   ");                      \
  } while (false)

} // namespace Envoy
