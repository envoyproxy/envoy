#include "source/extensions/access_loggers/common/access_log_base.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Common {

void ImplBase::log(const Http::RequestHeaderMap* request_headers,
                   const Http::ResponseHeaderMap* response_headers,
                   const Http::ResponseTrailerMap* response_trailers,
                   const StreamInfo::StreamInfo& stream_info,
                   AccessLog::AccessLogType access_log_type) {
  Formatter::HttpFormatterContext log_context{
      request_headers, response_headers, response_trailers, {}, access_log_type};

  if (!request_headers) {
    request_headers = Http::StaticEmptyHeaders::get().request_headers.get();
  }
  if (!response_headers) {
    response_headers = Http::StaticEmptyHeaders::get().response_headers.get();
  }
  if (!response_trailers) {
    response_trailers = Http::StaticEmptyHeaders::get().response_trailers.get();
  }
  if (filter_ && !filter_->evaluate(stream_info, *request_headers, *response_headers,
                                    *response_trailers, access_log_type)) {
    return;
  }

  return emitLog(log_context, stream_info);
}

} // namespace Common
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
