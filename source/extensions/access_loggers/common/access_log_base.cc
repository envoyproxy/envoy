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

  if (filter_ && !filter_->evaluate(log_context, stream_info)) {
    return;
  }

  return emitLog(log_context, stream_info);
}

} // namespace Common
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
