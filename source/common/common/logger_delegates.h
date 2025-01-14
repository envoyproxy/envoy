#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"

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

} // namespace Logger

} // namespace Envoy
