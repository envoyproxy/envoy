#include "extensions/access_loggers/common/access_log_base.h"

#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Common {

void ImplBase::log(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
                   const Http::HeaderMap* response_trailers,
                   const StreamInfo::StreamInfo& stream_info) {
  const Http::HeaderMapImpl empty_headers;
  if (!request_headers) {
    request_headers = &empty_headers;
  }
  if (!response_headers) {
    response_headers = &empty_headers;
  }
  if (!response_trailers) {
    response_trailers = &empty_headers;
  }
  if (filter_ &&
      !filter_->evaluate(stream_info, *request_headers, *response_headers, *response_trailers)) {
    return;
  }
  return emitLog(*request_headers, *response_headers, *response_trailers, stream_info);
}

} // namespace Common
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
