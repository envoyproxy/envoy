#include "common/common/logger_delegates.h"

#include <cassert> // use direct system-assert to avoid cyclic dependency.
#include <cstdint>
#include <iostream>
#include <string>

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Logger {

FileSinkDelegate::FileSinkDelegate(const std::string& log_path,
                                   AccessLog::AccessLogManager& log_manager,
                                   DelegatingLogSinkPtr log_sink)
    : SinkDelegate(log_sink), log_file_(log_manager.createAccessLog(log_path)) {}

void FileSinkDelegate::log(absl::string_view msg) {
  // Log files have internal locking to ensure serial, non-interleaved
  // writes, so no additional locking needed here.
  log_file_->write(msg);
}

void FileSinkDelegate::flush() {
  // Log files have internal locking to ensure serial, non-interleaved
  // writes, so no additional locking needed here.
  log_file_->flush();
}

} // namespace Logger
} // namespace Envoy
