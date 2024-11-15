#pragma once

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/http/header_map.h"

#include "source/common/tracing/null_span_impl.h"

namespace Envoy {
namespace Formatter {

using AccessLogType = envoy::data::accesslog::v3::AccessLogType;

/**
 * HTTP specific substitution formatter context for HTTP access logs or formatters.
 * TODO(wbpcode): maybe we should move this class to envoy/http folder and rename it
 * for more general usage.
 */
class HttpFormatterContext {
public:
  /**
   * Constructor that uses the provided request/response headers, response trailers, local reply
   * body, and access log type. Any of the parameters can be nullptr/empty.
   *
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param local_reply_body supplies the local reply body.
   * @param log_type supplies the access log type.
   * @param active_span supplies the active span.
   */
  HttpFormatterContext(const Http::RequestHeaderMap* request_headers = nullptr,
                       const Http::ResponseHeaderMap* response_headers = nullptr,
                       const Http::ResponseTrailerMap* response_trailers = nullptr,
                       absl::string_view local_reply_body = {},
                       AccessLogType log_type = AccessLogType::NotSet,
                       const Tracing::Span* active_span = nullptr);
  /**
   * Set or overwrite the request headers.
   * @param request_headers supplies the request headers.
   */
  HttpFormatterContext& setRequestHeaders(const Http::RequestHeaderMap& request_headers) {
    request_headers_ = &request_headers;
    return *this;
  }
  /**
   * Set or overwrite the response headers.
   * @param response_headers supplies the response headers.
   */
  HttpFormatterContext& setResponseHeaders(const Http::ResponseHeaderMap& response_headers) {
    response_headers_ = &response_headers;
    return *this;
  }

  /**
   * Set or overwrite the response trailers.
   * @param response_trailers supplies the response trailers.
   */
  HttpFormatterContext& setResponseTrailers(const Http::ResponseTrailerMap& response_trailers) {
    response_trailers_ = &response_trailers;
    return *this;
  }

  /**
   * Set or overwrite the local reply body.
   * @param local_reply_body supplies the local reply body.
   */
  HttpFormatterContext& setLocalReplyBody(absl::string_view local_reply_body) {
    local_reply_body_ = local_reply_body;
    return *this;
  }

  /**
   * Set or overwrite the access log type.
   * @param log_type supplies the access log type.
   */
  HttpFormatterContext& setAccessLogType(AccessLogType log_type) {
    log_type_ = log_type;
    return *this;
  }

  /**
   * @return const Http::RequestHeaderMap& the request headers. Empty request header map if no
   * request headers are available.
   */
  const Http::RequestHeaderMap& requestHeaders() const;

  /**
   * @return false if no request headers are available.
   */
  bool hasRequestHeaders() const { return request_headers_ != nullptr; }

  /**
   * @return const Http::ResponseHeaderMap& the response headers. Empty response header map if
   * no response headers are available.
   */
  const Http::ResponseHeaderMap& responseHeaders() const;

  /**
   * @return false if no response headers are available.
   */
  bool hasResponseHeaders() const { return response_headers_ != nullptr; }

  /**
   * @return const Http::ResponseTrailerMap& the response trailers. Empty response trailer map
   * if no response trailers are available.
   */
  const Http::ResponseTrailerMap& responseTrailers() const;

  /**
   * @return false if no response trailers are available.
   */
  bool hasResponseTrailers() const { return response_trailers_ != nullptr; }

  /**
   * @return absl::string_view the local reply body. Empty if no local reply body.
   */
  absl::string_view localReplyBody() const;

  /**
   * @return AccessLog::AccessLogType the type of access log. NotSet if this is not used for
   * access logging.
   */
  AccessLogType accessLogType() const;

  /**
   * @return const Tracing::Span& the active span.
   */
  const Tracing::Span& activeSpan() const;

  /**
   * @return short category name of the HttpFormatterContext. This may be used to create
   * an actual fully qualified category name for other factory classes that use this context.
   * For example the http specific built-in formatters will have a category of
   * "envoy.built_in_formatters.http".
   */
  static constexpr absl::string_view category() { return "http"; }

private:
  const Http::RequestHeaderMap* request_headers_{};
  const Http::ResponseHeaderMap* response_headers_{};
  const Http::ResponseTrailerMap* response_trailers_{};
  absl::string_view local_reply_body_{};
  AccessLogType log_type_{AccessLogType::NotSet};
  const Tracing::Span* active_span_ = nullptr;
};

} // namespace Formatter
} // namespace Envoy
