#pragma once

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Logger {

/* This is simple mapping between Logger severity levels and spdlog severity levels.
 * The only reason for this mapping is to go around the fact that spdlog defines level as err
 * but the method to log at err level is called LOGGER.error not LOGGER.err. All other level are
 * fine spdlog::info corresponds to LOGGER.info method.
 */
enum class Levels {
  trace = spdlog::level::trace,       // NOLINT(readability-identifier-naming)
  debug = spdlog::level::debug,       // NOLINT(readability-identifier-naming)
  info = spdlog::level::info,         // NOLINT(readability-identifier-naming)
  warn = spdlog::level::warn,         // NOLINT(readability-identifier-naming)
  error = spdlog::level::err,         // NOLINT(readability-identifier-naming)
  critical = spdlog::level::critical, // NOLINT(readability-identifier-naming)
  off = spdlog::level::off            // NOLINT(readability-identifier-naming)
};

} // namespace Logger
} // namespace Envoy
