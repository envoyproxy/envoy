#include "source/common/common/win32/event_logger_impl.h"

#include "spdlog/sinks/win_eventlog_sink.h"

namespace Envoy {
namespace Logger {

// TODO(davinci26): When we start supporting a resources file we can add
// a specific event id for envoy errors.
WindowsEventLogger::WindowsEventLogger(const std::string& name)
    : Logger(std::make_shared<spdlog::logger>(
          name, std::make_shared<spdlog::sinks::win_eventlog_sink_mt>("envoy"))) {}

} // namespace Logger
} // namespace Envoy
