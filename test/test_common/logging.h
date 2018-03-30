#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/common/logger.h"

#include "spdlog/spdlog.h"

namespace Envoy {

/**
 * Provides a mechanism to temporarily set the logging level on
 * construction, restoring its previous state on dstruction.
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

  // Logger::SinkDelgate
  void log(absl::string_view msg) override;
  void flush() override;

  const std::vector<std::string>& messages() const { return messages_; }

private:
  std::vector<std::string> messages_;
};

// Validates that when stmt is executed, exactly one log message containing substr will be emitted.
#define EXPECT_LOG_CONTAINS(substr, stmt)                                                          \
  do {                                                                                             \
    LogLevelSetter save_levels(spdlog::level::trace);                                              \
    LogRecordingSink log_recorder(Logger::Registry::getSink());                                    \
    stmt;                                                                                          \
    ASSERT_EQ(1, log_recorder.messages().size());                                                  \
    std::string recorded_log = log_recorder.messages()[0];                                         \
    EXPECT_TRUE(absl::string_view(recorded_log).find(substr) != absl::string_view::npos)           \
        << "\n Actual Log:         " << recorded_log << "\n Expected Substring: " << substr;       \
  } while (false)

// Validates that when stmt is executed, exactly logs will be emitted.
#define EXPECT_NO_LOGS(stmt)                                                                       \
  do {                                                                                             \
    LogLevelSetter save_levels(spdlog::level::trace);                                              \
    LogRecordingSink log_recorder(Logger::Registry::getSink());                                    \
    stmt;                                                                                          \
    ASSERT_EQ(0, log_recorder.messages().size()) << "msg[0]: " << log_recorder.messages()[0];      \
  } while (false)

} // namespace Envoy
