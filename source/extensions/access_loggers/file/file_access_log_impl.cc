#include "extensions/access_loggers/file/file_access_log_impl.h"

#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {

FileAccessLog::FileAccessLog(const std::string& access_log_path, AccessLog::FilterPtr&& filter,
                             AccessLog::FormatterPtr&& formatter,
                             AccessLog::AccessLogManager& log_manager)
    : filter_(std::move(filter)), formatter_(std::move(formatter)) {
  log_file_ = log_manager.createAccessLog(access_log_path);
}

void FileAccessLog::log(const Http::HeaderMap* request_headers,
                        const Http::HeaderMap* response_headers,
                        const RequestInfo::RequestInfo& request_info) {
  static Http::HeaderMapImpl empty_headers;
  if (!request_headers) {
    request_headers = &empty_headers;
  }
  if (!response_headers) {
    response_headers = &empty_headers;
  }

  if (filter_) {
    if (!filter_->evaluate(request_info, *request_headers)) {
      return;
    }
  }

  log_file_->write(formatter_->format(*request_headers, *response_headers, request_info));
}

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
