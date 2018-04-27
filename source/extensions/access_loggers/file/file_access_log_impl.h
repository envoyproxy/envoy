#pragma once

#include "envoy/access_log/access_log.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {

/**
 * Access log Instance that writes logs to a file.
 */
class FileAccessLog : public AccessLog::Instance {
public:
  FileAccessLog(const std::string& access_log_path, AccessLog::FilterPtr&& filter,
                AccessLog::FormatterPtr&& formatter, AccessLog::AccessLogManager& log_manager);

  // AccessLog::Instance
  void log(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
           const Http::HeaderMap* response_trailers,
           const RequestInfo::RequestInfo& request_info) override;

private:
  Filesystem::FileSharedPtr log_file_;
  AccessLog::FilterPtr filter_;
  AccessLog::FormatterPtr formatter_;
};

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
