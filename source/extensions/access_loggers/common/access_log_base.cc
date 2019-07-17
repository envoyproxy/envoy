#include "extensions/access_loggers/common/access_log_base.h"

#include "common/http/header_map_impl.h"
#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Common {

void ImplBase::log(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
                   const Http::HeaderMap* response_trailers,
                   const StreamInfo::StreamInfo& stream_info) {
  ConstSingleton<Http::HeaderMapImpl> empty_headers;
  if (!request_headers) {
    request_headers = &empty_headers.get();
  }
  if (!response_headers) {
    response_headers = &empty_headers.get();
  }
  if (!response_trailers) {
    response_trailers = &empty_headers.get();
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
