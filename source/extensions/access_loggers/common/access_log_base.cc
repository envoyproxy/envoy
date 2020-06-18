#include "extensions/access_loggers/common/access_log_base.h"

#include "common/http/header_map_impl.h"
#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Common {

void ImplBase::log(const Http::RequestHeaderMap* request_headers,
                   const Http::ResponseHeaderMap* response_headers,
                   const Http::ResponseTrailerMap* response_trailers,
                   const StreamInfo::StreamInfo& stream_info) {
  if (!request_headers) {
    request_headers = Http::StaticEmptyHeaders::get().request_headers.get();
  }
  if (!response_headers) {
    response_headers = Http::StaticEmptyHeaders::get().response_headers.get();
  }
  if (!response_trailers) {
    response_trailers = Http::StaticEmptyHeaders::get().response_trailers.get();
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
