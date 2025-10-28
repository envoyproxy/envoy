#pragma once

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/http/header_map.h"

#include "source/common/tracing/null_span_impl.h"

namespace Envoy {
namespace Formatter {

using AccessLogType = envoy::data::accesslog::v3::AccessLogType;

/**
 * HTTP specific substitution formatter context for HTTP access logs or formatters.
 */
class Context {
public:
  /**
   * Interface for a context extension which can be used to provide non-HTTP specific data to
   * formatters. This could be used for non-HTTP protocols to provide protocol specific data to
   * formatters.
   */
  class Extension {
  public:
    virtual ~Extension() = default;
  };

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
  Context(const Http::RequestHeaderMap* request_headers = nullptr,
          const Http::ResponseHeaderMap* response_headers = nullptr,
          const Http::ResponseTrailerMap* response_trailers = nullptr,
          absl::string_view local_reply_body = {}, AccessLogType log_type = AccessLogType::NotSet,
          const Tracing::Span* active_span = nullptr)
      : local_reply_body_(local_reply_body), request_headers_(makeOptRefFromPtr(request_headers)),
        response_headers_(makeOptRefFromPtr(response_headers)),
        response_trailers_(makeOptRefFromPtr(response_trailers)),
        active_span_(makeOptRefFromPtr(active_span)), log_type_(log_type) {}

  /**
   * Set or overwrite the request headers.
   * @param request_headers supplies the request headers.
   */
  Context& setRequestHeaders(const Http::RequestHeaderMap& request_headers) {
    request_headers_ = request_headers;
    return *this;
  }

  /**
   * Set or overwrite the response headers.
   * @param response_headers supplies the response headers.
   */
  Context& setResponseHeaders(const Http::ResponseHeaderMap& response_headers) {
    response_headers_ = response_headers;
    return *this;
  }

  /**
   * Set or overwrite the response trailers.
   * @param response_trailers supplies the response trailers.
   */
  Context& setResponseTrailers(const Http::ResponseTrailerMap& response_trailers) {
    response_trailers_ = response_trailers;
    return *this;
  }

  /**
   * Set or overwrite the local reply body.
   * @param local_reply_body supplies the local reply body.
   */
  Context& setLocalReplyBody(absl::string_view local_reply_body) {
    local_reply_body_ = local_reply_body;
    return *this;
  }

  /**
   * Set or overwrite the access log type.
   * @param log_type supplies the access log type.
   */
  Context& setAccessLogType(AccessLogType log_type) {
    log_type_ = log_type;
    return *this;
  }

  /**
   * @return OptRef<const Http::RequestHeaderMap> the request headers.
   */
  OptRef<const Http::RequestHeaderMap> requestHeaders() const { return request_headers_; }

  /**
   * @return OptRef<const Http::ResponseHeaderMap> the response headers.
   */
  OptRef<const Http::ResponseHeaderMap> responseHeaders() const { return response_headers_; }

  /**
   * @return OptRef<const Http::ResponseTrailerMap> the response trailers.
   */
  OptRef<const Http::ResponseTrailerMap> responseTrailers() const { return response_trailers_; }

  /**
   * @return absl::string_view the local reply body. Empty if no local reply body.
   */
  absl::string_view localReplyBody() const { return local_reply_body_; }

  /**
   * @return AccessLog::AccessLogType the type of access log. NotSet if this is not used for
   * access logging.
   */
  AccessLogType accessLogType() const { return log_type_; }

  /**
   * Set or overwrite the active span.
   * @param active_span supplies the active span.
   */
  Context& setActiveSpan(const Tracing::Span& active_span) {
    active_span_ = makeOptRefFromPtr(&active_span);
    return *this;
  }

  /**
   * @return OptRef<const Tracing::Span> the active span.
   */
  OptRef<const Tracing::Span> activeSpan() const { return active_span_; }

  /**
   * Set the context extension.
   * @param extension supplies the context extension.
   */
  Context& setExtension(const Extension& extension) {
    extension_ = extension;
    return *this;
  }

  /**
   * @return OptRef<const ContextExtension> the context extension.
   */
  OptRef<const Extension> extension() const { return extension_; }

  /**
   * @return OptRef<const ExtensionType> the context extension casted to the specified type.
   */
  template <class Type> OptRef<const Type> typedExtension() const {
    const Type* typed_extension = dynamic_cast<const Type*>(extension_.ptr());
    return makeOptRefFromPtr(typed_extension);
  }

private:
  absl::string_view local_reply_body_;
  OptRef<const Http::RequestHeaderMap> request_headers_;
  OptRef<const Http::ResponseHeaderMap> response_headers_;
  OptRef<const Http::ResponseTrailerMap> response_trailers_;
  OptRef<const Extension> extension_;
  OptRef<const Tracing::Span> active_span_;
  AccessLogType log_type_{AccessLogType::NotSet};
};

} // namespace Formatter
} // namespace Envoy
