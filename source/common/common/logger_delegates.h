#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"

#include "common/common/logger.h"
#include "common/common/macros.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Logger {

class DelegatingLogSink;
using DelegatingLogSinkSharedPtr = std::shared_ptr<DelegatingLogSink>;

/**
 * SinkDelegate that writes log messages to a file.
 */
class FileSinkDelegate : public SinkDelegate {
public:
  FileSinkDelegate(const std::string& log_path, AccessLog::AccessLogManager& log_manager,
                   DelegatingLogSinkSharedPtr log_sink);

  // SinkDelegate
  void log(absl::string_view msg) override;
  void flush() override;

private:
  AccessLog::AccessLogFileSharedPtr log_file_;
};

} // namespace Logger

} // namespace Envoy
