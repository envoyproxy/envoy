#include "source/common/common/logger_delegates.h"

#include <cassert> // use direct system-assert to avoid cyclic dependency.
#include <cstdint>
#include <iostream>
#include <string>

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Logger {
FileSinkDelegate::FileSinkDelegate(const std::string& log_path,
                                   AccessLog::AccessLogManager& log_manager,
                                   DelegatingLogSinkSharedPtr log_sink)
    : SinkDelegate(log_sink) {
  auto file_or_error = log_manager.createAccessLog(
      Filesystem::FilePathAndType{Filesystem::DestinationType::File, log_path});
  THROW_IF_NOT_OK_REF(file_or_error.status());
  log_file_ = file_or_error.value();
  setDelegate();
}

FileSinkDelegate::~FileSinkDelegate() { restoreDelegate(); }

void FileSinkDelegate::log(absl::string_view msg, const spdlog::details::log_msg&) {
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
