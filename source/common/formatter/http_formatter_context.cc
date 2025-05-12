#include "envoy/formatter/http_formatter_context.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Formatter {

HttpFormatterContext::HttpFormatterContext(const Http::RequestHeaderMap* request_headers,
                                           const Http::ResponseHeaderMap* response_headers,
                                           const Http::ResponseTrailerMap* response_trailers,
                                           absl::string_view local_reply_body,
                                           AccessLog::AccessLogType log_type,
                                           const Tracing::Span* active_span)
    : request_headers_(request_headers), response_headers_(response_headers),
      response_trailers_(response_trailers), local_reply_body_(local_reply_body),
      log_type_(log_type), active_span_(active_span) {}

const Http::RequestHeaderMap& HttpFormatterContext::requestHeaders() const {
  return request_headers_ != nullptr ? *request_headers_
                                     : *Http::StaticEmptyHeaders::get().request_headers;
}
const Http::ResponseHeaderMap& HttpFormatterContext::responseHeaders() const {
  return response_headers_ != nullptr ? *response_headers_
                                      : *Http::StaticEmptyHeaders::get().response_headers;
}
const Http::ResponseTrailerMap& HttpFormatterContext::responseTrailers() const {
  return response_trailers_ != nullptr ? *response_trailers_
                                       : *Http::StaticEmptyHeaders::get().response_trailers;
}

absl::string_view HttpFormatterContext::localReplyBody() const { return local_reply_body_; }
AccessLog::AccessLogType HttpFormatterContext::accessLogType() const { return log_type_; }
const Tracing::Span& HttpFormatterContext::activeSpan() const {
  if (active_span_ == nullptr) {
    return Tracing::NullSpan::instance();
  }

  return *active_span_;
}

static constexpr absl::string_view DEFAULT_FORMAT =
    "[%START_TIME%] \"%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%\" "
    "%RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %DURATION% "
    "%RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% "
    "\"%REQ(X-FORWARDED-FOR)%\" \"%REQ(USER-AGENT)%\" \"%REQ(X-REQUEST-ID)%\" "
    "\"%REQ(:AUTHORITY)%\" \"%UPSTREAM_HOST%\"\n";

absl::StatusOr<FormatterPtr> HttpSubstitutionFormatUtils::defaultSubstitutionFormatter() {
  // It is possible that failed to parse the default format string if the required formatters
  // are compiled out.
  return Envoy::Formatter::FormatterImpl::create(DEFAULT_FORMAT, false);
}

} // namespace Formatter
} // namespace Envoy
