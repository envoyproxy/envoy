#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/config/filter/accesslog/v2/accesslog.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/access_log_config.h"

#include "common/http/header_map_impl.h"
#include "common/http/header_utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Common {

/**
 * Base implementation of Accesslog::Instance handles common filter logic.
 */
class ImplBase : public AccessLog::Instance {
public:
  ImplBase(AccessLog::FilterPtr filter) : filter_(std::move(filter)) {}

  /**
   * Log a completed request if the underlying AccessLog `filter_` allows it.
   */
  virtual void log(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
                   const Http::HeaderMap* response_trailers,
                   const StreamInfo::StreamInfo& stream_info) override {
    static Http::HeaderMapImpl empty_headers;
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

private:
  /**
   * Log a completed request.
   * @param request_headers supplies the incoming request headers after filtering.
   * @param response_headers supplies response headers.
   * @param response_trailers supplies response trailers.
   * @param stream_info supplies additional information about the request not
   * contained in the request headers.
   */
  virtual void emitLog(const Http::HeaderMap& request_headers,
                       const Http::HeaderMap& response_headers,
                       const Http::HeaderMap& response_trailers,
                       const StreamInfo::StreamInfo& stream_info) PURE;

  AccessLog::FilterPtr filter_;
};

} // namespace Common
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
