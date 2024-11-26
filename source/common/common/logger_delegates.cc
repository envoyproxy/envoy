#include "source/common/common/logger_delegates.h"

#include <cassert> // use direct system-assert to avoid cyclic dependency.
#include <cstdint>
#include <iostream>
#include <string>

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Logger {
absl::StatusOr<std::unique_ptr<FileSinkDelegate>>
FileSinkDelegate::create(const std::string& log_path, AccessLog::AccessLogManager& log_manager,
                         DelegatingLogSinkSharedPtr log_sink) {
  auto file_or_error = log_manager.createAccessLog(
      Filesystem::FilePathAndType{Filesystem::DestinationType::File, log_path});
  RETURN_IF_NOT_OK_REF(file_or_error.status());
  return std::unique_ptr<FileSinkDelegate>(
      new FileSinkDelegate(std::move(*file_or_error), log_sink));
}

FileSinkDelegate::FileSinkDelegate(AccessLog::AccessLogFileSharedPtr&& log_file,
                                   DelegatingLogSinkSharedPtr log_sink)
    : SinkDelegate(log_sink), log_file_(std::move(log_file)) {
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
