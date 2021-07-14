#pragma once

#include "source/common/common/base_logger.h"

namespace Envoy {
namespace Logger {

#define GENERATE_WIN32_NATIVE_LOGGER(X) WindowsEventLogger(#X),

/**
 * Logger that uses `spdlog::sinks::win_eventlog_sink`.
 */
class WindowsEventLogger : public Logger {
public:
  WindowsEventLogger(const std::string& name);
};

} // namespace Logger
} // namespace Envoy
