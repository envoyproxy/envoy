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
 */
class LogRecordingSink : public Logger::SinkDelegate {
public:
  LogRecordingSink();
  virtual ~LogRecordingSink();

  // Logger::SinkDelgate
  void log(absl::string_view msg) override;
  void flush() override;

  const std::vector<std::string>& messages() const { return messages_; }

private:
  std::vector<std::string> messages_;
};

} // namespace Envoy
