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
 * construction, restoring its previous state on deconstruction.
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

typedef std::vector<std::pair<std::string, std::string>> ExpectedLoggingPairs;

// Validates that when the stmt is executed, a sequence of emmitted logs matches a sequence of
// expected log messages and log level pairs (ExpectedLoggingPairs). Note that both sequences must
// respect the same order.
#define EXPECT_LOG_SEQ(expected_logging_pairs, stmt)                                               \
  do {                                                                                             \
    LogLevelSetter save_levels(spdlog::level::trace);                                              \
    LogRecordingSink log_recorder(Logger::Registry::getSink());                                    \
    stmt;                                                                                          \
    ASSERT_EQ(expected_logging_pairs.size(), log_recorder.messages().size());                      \
    auto it1 = expected_logging_pairs.begin();                                                     \
    auto it2 = log_recorder.messages().begin();                                                    \
    for (; it1 != expected_logging_pairs.end() && it2 != log_recorder.messages().end();            \
         ++it1, ++it2) {                                                                           \
      std::vector<absl::string_view> pieces = absl::StrSplit(*it2, "][");                          \
      const auto expected_log_level = it1->first;                                                  \
      const auto actual_log_level = std::string(pieces[2]);                                        \
      const auto actual_log_message = absl::string_view(pieces[3]).find(it1->second);              \
      EXPECT_EQ(expected_log_level, actual_log_level);                                             \
      EXPECT_NE(actual_log_message, absl::string_view::npos);                                      \
    }                                                                                              \
  } while (false)

// Validates that when stmt is executed, exactly one log message containing substr will be emitted.
#define EXPECT_LOG_CONTAINS(loglevel, substr, stmt)                                                \
  do {                                                                                             \
    LogLevelSetter save_levels(spdlog::level::trace);                                              \
    LogRecordingSink log_recorder(Logger::Registry::getSink());                                    \
    stmt;                                                                                          \
    ASSERT_EQ(1, log_recorder.messages().size());                                                  \
    std::string recorded_log = log_recorder.messages()[0];                                         \
    std::vector<absl::string_view> pieces = absl::StrSplit(recorded_log, "][");                    \
    /* Parse "[2018-04-02 19:06:08.629][15][warn][admin] source/file.cc:691] message ..." */       \
    ASSERT_LE(3, pieces.size());                                                                   \
    EXPECT_EQ(loglevel, std::string(pieces[2])); /* error message is legible if cast to string */  \
    EXPECT_TRUE(absl::string_view(recorded_log).find(substr) != absl::string_view::npos)           \
        << "\n Actual Log:         " << recorded_log << "\n Expected Substring: " << substr;       \
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
