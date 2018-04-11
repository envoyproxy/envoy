#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/common/logger.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "spdlog/spdlog.h"

namespace Envoy {

/**
 * Provides a mechanism to temporarily set the logging level on
 * construction, restoring its previous state on destruction.
 *
 * The log_level is the minimum log severity required to print messages.
 * Messages below this loglevel will be suppressed.
 *
 * Note that during the scope of this object, command-line overrides, eg
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
  explicit LogRecordingSink(Logger::DelegatingLogSinkPtr log_sink);
  virtual ~LogRecordingSink();

  // Logger::SinkDelegate
  void log(absl::string_view msg) override;
  void flush() override;

  const std::vector<std::string>& messages() const { return messages_; }

private:
  std::vector<std::string> messages_;
};

typedef std::vector<std::pair<std::string, std::string>> ExpectedLogSequence;

// Validates that when the stmt is executed, a sequence of emmitted log messages matches the
// sequence of expected logs/log level pair (ExpectedLogSequence). Note that both sequences must
// respect the same order.
#define EXPECT_LOG_SEQ(expected_log_sequence, stmt)                                                \
  do {                                                                                             \
    LogLevelSetter save_levels(spdlog::level::trace);                                              \
    LogRecordingSink log_recorder(Logger::Registry::getSink());                                    \
    stmt;                                                                                          \
    std::string expected_log;                                                                      \
    for (auto expected : expected_log_sequence) {                                                  \
      absl::StrAppend(&expected_log, "[", expected.first, "]", expected.second, "\n");             \
    }                                                                                              \
    std::string actual_log;                                                                        \
    for (auto message : log_recorder.messages()) {                                                 \
      std::vector<absl::string_view> pieces = absl::StrSplit(message, "][");                       \
      absl::StrAppend(&actual_log, message);                                                       \
    }                                                                                              \
    if (log_recorder.messages().size() != expected_log_sequence.size()) {                          \
      FAIL() << "\nExpected:\n"                                                                    \
             << expected_log << "\nTo be subset of:\n"                                             \
             << (actual_log.size() > 0 ? actual_log : "No messages.");                             \
    }                                                                                              \
    for (uint64_t i = 0; i < expected_log_sequence.size(); i++) {                                  \
      const std::string actual_message = log_recorder.messages()[i];                               \
      /* Parse "[2018-04-02 19:06:08.629][15][warn][admin] source/file.cc:691] message ..." */     \
      std::vector<absl::string_view> pieces = absl::StrSplit(actual_message, "][");                \
      ASSERT_LE(3, pieces.size());                                                                 \
      const std::string actual_level{pieces[2]};                                                   \
      const std::string expected_level = expected_log_sequence[i].first;                           \
      const std::string expected_substr = expected_log_sequence[i].second;                         \
      if (expected_level != actual_level) {                                                        \
        FAIL() << "\nExpected log message:\n"                                                      \
               << actual_message << "\nTo contain log level:\n"                                    \
               << expected_level << "\nBut found:\n"                                               \
               << actual_level;                                                                    \
      }                                                                                            \
      if (actual_message.find(expected_substr) == absl::string_view::npos) {                       \
        FAIL() << "\nActual log message:\n"                                                        \
               << actual_message << "\nDoes NOT contain the expected substring:\n"                 \
               << expected_substr;                                                                 \
      }                                                                                            \
    }                                                                                              \
  } while (false)

// Validates that when stmt is executed, exactly one log message containing substr will be emitted.
#define EXPECT_LOG_CONTAINS(loglevel, substr, stmt)                                                \
  do {                                                                                             \
    LogLevelSetter save_levels(spdlog::level::trace);                                              \
    LogRecordingSink log_recorder(Logger::Registry::getSink());                                    \
    const ExpectedLogSequence expected_log = {{loglevel, substr}};                                 \
    EXPECT_LOG_SEQ(expected_log, stmt);                                                            \
  } while (false)

// Validates that when stmt is executed, no logs will be emitted.
#define EXPECT_NO_LOGS(stmt)                                                                       \
  do {                                                                                             \
    LogLevelSetter save_levels(spdlog::level::trace);                                              \
    LogRecordingSink log_recorder(Logger::Registry::getSink());                                    \
    stmt;                                                                                          \
    const std::vector<std::string> logs = log_recorder.messages();                                 \
    ASSERT_EQ(0, logs.size()) << " Logs:\n   " << absl::StrJoin(logs, "\n  ");                     \
  } while (false)

} // namespace Envoy
