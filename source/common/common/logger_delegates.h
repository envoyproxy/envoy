#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/api/api.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/common/macros.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Logger {

/**
 * SinkDelegate that writes log messages to a file.
 */
class FileSinkDelegate : public SinkDelegate {
public:
  static absl::StatusOr<std::unique_ptr<FileSinkDelegate>>
  create(const std::string& log_path, AccessLog::AccessLogManager& log_manager,
         DelegatingLogSinkSharedPtr log_sink);
  ~FileSinkDelegate() override;

  // SinkDelegate
  void log(absl::string_view msg, const spdlog::details::log_msg& log_msg) override;
  void flush() override;

protected:
  FileSinkDelegate(AccessLog::AccessLogFileSharedPtr&& log_file,
                   DelegatingLogSinkSharedPtr log_sink);

private:
  AccessLog::AccessLogFileSharedPtr log_file_;
};

#define EVENT_PIPE_STATS(COUNTER) COUNTER(write_failed)

struct EventPipeStats {
  EVENT_PIPE_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Event sink delegate using a pipe. Note that writes to pipes are atomic as
 * long as each write size is below PIPE_BUF (which is 64kB on Linux). As long as
 * the serialized events are under this size, there is no need for
 * synchronization, unlike the regular logs.
 */
class EventPipeDelegate : public SinkDelegate {
public:
  static absl::StatusOr<std::unique_ptr<EventPipeDelegate>>
  create(Api::Api& api, const std::string& log_path, Stats::Store& stats_store,
         DelegatingLogSinkSharedPtr log_sink);
  ~EventPipeDelegate() override;

  // SinkDelegate
  void log(absl::string_view, const spdlog::details::log_msg& log_msg) override;
  void logWithStableName(absl::string_view stable_name, absl::string_view level,
                         absl::string_view component, absl::string_view msg) override;
  void flush() override;

protected:
  EventPipeDelegate(Filesystem::FilePtr file, Stats::Store& stats_store,
                    DelegatingLogSinkSharedPtr log_sink);

private:
  Filesystem::FilePtr file_;
  EventPipeStats stats_;
};

} // namespace Logger

} // namespace Envoy
