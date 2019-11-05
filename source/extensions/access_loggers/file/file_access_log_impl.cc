#include "extensions/access_loggers/file/file_access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {

FileAccessLog::FileAccessLog(const std::string& access_log_path, AccessLog::FilterPtr&& filter,
                             AccessLog::FormatterPtr&& formatter,
                             AccessLog::AccessLogManager& log_manager)
    : ImplBase(std::move(filter)), formatter_(std::move(formatter)) {
  log_file_ = log_manager.createAccessLog(access_log_path);
}

void FileAccessLog::emitLog(const Http::HeaderMap& request_headers,
                            const Http::HeaderMap& response_headers,
                            const Http::HeaderMap& response_trailers,
                            const StreamInfo::StreamInfo& stream_info) {
  log_file_->write(formatter_->format(request_headers, response_headers, response_trailers,
                                      stream_info, std::string()));
}

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
