#pragma once

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Logger {
/**
 * Logger wrapper for a spdlog logger.
 */
class Logger {
public:
  spdlog::string_view_t levelString() const {
    return spdlog::level::level_string_views[logger_->level()];
  }
  std::string name() const { return logger_->name(); }
  void setLevel(spdlog::level::level_enum level) { logger_->set_level(level); }
  spdlog::level::level_enum level() const { return logger_->level(); }
  spdlog::logger& getLogger() { return *logger_; }

  /*
   * Exposes the log method of the logger. See `spdlog::logger` log method.
   */
  template <typename... Args>
  void log(spdlog::source_loc loc, spdlog::level::level_enum lvl, absl::string_view fmt,
           const Args&... args) {
    logger_->log(loc, lvl, fmt, args...);
  }

  static const char* DEFAULT_LOG_FORMAT;

protected:
  Logger(std::shared_ptr<spdlog::logger> logger);

private:
  std::shared_ptr<spdlog::logger> logger_; // Use shared_ptr here to allow static construction
                                           // of vector in Registry::allLoggers().
};

} // namespace Logger
} // namespace Envoy
