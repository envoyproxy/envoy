#pragma once

#include "envoy/access_log/access_log.h"

#include "common/access_log/access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {

/**
 * Access log Instance that writes logs to a file.
 */
class FileAccessLog : public AccessLog::InstanceImpl {
public:
  FileAccessLog(const std::string& access_log_path, AccessLog::FilterPtr&& filter,
                AccessLog::FormatterPtr&& formatter, AccessLog::AccessLogManager& log_manager);

private:
  // AccessLog::InstanceImpl
  void log(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
           const Http::HeaderMap* response_trailers,
           const StreamInfo::StreamInfo& stream_info) override;

  AccessLog::AccessLogFileSharedPtr log_file_;
  AccessLog::FormatterPtr formatter_;
};

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
