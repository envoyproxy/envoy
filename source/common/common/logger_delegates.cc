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

EventPipeDelegate::EventPipeDelegate(Filesystem::FilePtr file, Stats::Store& stats_store,
                                     DelegatingLogSinkSharedPtr log_sink)
    : SinkDelegate(std::move(log_sink)), file_(std::move(file)),
      stats_{EVENT_PIPE_STATS(POOL_COUNTER_PREFIX(stats_store, "event_log."))} {
  setDelegate();
}

EventPipeDelegate::~EventPipeDelegate() { restoreDelegate(); }

void EventPipeDelegate::log(absl::string_view msg, const spdlog::details::log_msg& log_msg) {
  previousDelegate()->log(msg, log_msg);
}

void EventPipeDelegate::logWithStableName(absl::string_view stable_name, absl::string_view level,
                                          absl::string_view component, absl::string_view msg) {
  const std::string data =
      absl::StrCat("[", level, "] ", component, " ", stable_name, " ", msg, "\n");
  const Api::IoCallSizeResult result = file_->write(data);
  if (!result.ok()) {
    // Since O_NONBLOCK is enabled, EAGAIN is returned when there is no capacity in the file buffer.
    stats_.write_failed_.inc();
    if (result.return_value_ > 0 && result.return_value_ != static_cast<ssize_t>(data.size())) {
      ENVOY_LOG_EVERY_POW_2_MISC(warn, "Partial write to the event log, please increase PIPE_BUF "
                                       "size to be larger than the log entry size.");
    }
  }
  previousDelegate()->logWithStableName(stable_name, level, component, msg);
}

void EventPipeDelegate::flush() { previousDelegate()->flush(); }

namespace {
constexpr Filesystem::FlagSet DefaultFlags =
    1 << Filesystem::File::Operation::Write | 1 << Filesystem::File::Operation::Create |
    1 << Filesystem::File::Operation::Append | 1 << Filesystem::File::Operation::NonBlock;
}

absl::StatusOr<std::unique_ptr<EventPipeDelegate>>
EventPipeDelegate::create(Api::Api& api, const std::string& log_path, Stats::Store& stats_store,
                          DelegatingLogSinkSharedPtr log_sink) {
  Filesystem::FilePtr file = api.fileSystem().createFile(
      Filesystem::FilePathAndType{Filesystem::DestinationType::File, log_path});
  const Api::IoCallBoolResult open_result = file->open(DefaultFlags);
  if (!open_result.ok()) {
    return absl::InvalidArgumentError(
        fmt::format("unable to open file '{}': {}", log_path, open_result.err_->getErrorDetails()));
  }
  return std::unique_ptr<EventPipeDelegate>(
      new EventPipeDelegate(std::move(file), stats_store, std::move(log_sink)));
}

} // namespace Logger
} // namespace Envoy
