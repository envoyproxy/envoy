#pragma once

#include <memory>
#include <string>

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Logger {

/**
 * Logger wrapper for a spdlog logger.
 */
class Logger {
public:
  /* This is simple mapping between Logger severity levels and spdlog severity levels.
   * The only reason for this mapping is to go around the fact that spdlog defines level as err
   * but the method to log at err level is called LOGGER.error not LOGGER.err. All other level are
   * fine spdlog::info corresponds to LOGGER.info method.
   */
  using Levels = enum {
    trace = spdlog::level::trace,       // NOLINT(readability-identifier-naming)
    debug = spdlog::level::debug,       // NOLINT(readability-identifier-naming)
    info = spdlog::level::info,         // NOLINT(readability-identifier-naming)
    warn = spdlog::level::warn,         // NOLINT(readability-identifier-naming)
    error = spdlog::level::err,         // NOLINT(readability-identifier-naming)
    critical = spdlog::level::critical, // NOLINT(readability-identifier-naming)
    off = spdlog::level::off            // NOLINT(readability-identifier-naming)
  };

  spdlog::string_view_t levelString() const {
    return spdlog::level::level_string_views[logger_->level()];
  }
  std::string name() const { return logger_->name(); }
  void setLevel(spdlog::level::level_enum level) { logger_->set_level(level); }
  spdlog::level::level_enum level() const { return logger_->level(); }

  static const char* DEFAULT_LOG_FORMAT;

protected:
  Logger(std::shared_ptr<spdlog::logger> logger);

private:
  std::shared_ptr<spdlog::logger> logger_; // Use shared_ptr here to allow static construction
                                           // of vector in Registry::allLoggers().
  // TODO(junr03): expand Logger's public API to delete this friendship.
  friend class Registry;
};

} // namespace Logger
} // namespace Envoy
