#pragma once

#include "envoy/formatter/substitution_formatter_base.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Formatter {

/**
 * Substitution formatter context for HTTP access logs.
 */
class HttpFormatterContext {
public:
  /**
   * Constructor that uses the provided request/response headers, response trailers, local reply
   * body, and access log type.
   *
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param local_reply_body supplies the local reply body.
   * @param log_type supplies the access log type.
   */
  HttpFormatterContext(const Http::RequestHeaderMap& request_headers,
                       const Http::ResponseHeaderMap& response_headers,
                       const Http::ResponseTrailerMap& response_trailers,
                       absl::string_view local_reply_body, AccessLog::AccessLogType log_type);

  /**
   * Constructor that uses the provided request/response headers, response trailers, local reply
   * body, and access log type. Any of the parameters can be nullptr/empty.
   *
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param local_reply_body supplies the local reply body.
   * @param log_type supplies the access log type.
   */
  HttpFormatterContext(const Http::RequestHeaderMap* request_headers = nullptr,
                       const Http::ResponseHeaderMap* response_headers = nullptr,
                       const Http::ResponseTrailerMap* response_trailers = nullptr,
                       absl::string_view local_reply_body = {},
                       AccessLog::AccessLogType log_type = AccessLog::AccessLogType::NotSet);

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
  HttpFormatterContext& setAccessLogType(AccessLog::AccessLogType log_type) {
    log_type_ = log_type;
    return *this;
  }

  /**
   * @return const Http::RequestHeaderMap& the request headers. Empty request header map if no
   * request headers are available.
   */
  const Http::RequestHeaderMap& requestHeaders() const;

  /**
   * @return const Http::ResponseHeaderMap& the response headers. Empty respnose header map if
   * no response headers are available.
   */
  const Http::ResponseHeaderMap& responseHeaders() const;

  /**
   * @return const Http::ResponseTrailerMap& the response trailers. Empty response trailer map
   * if no response trailers are available.
   */
  const Http::ResponseTrailerMap& responseTrailers() const;

  /**
   * @return absl::string_view the local reply body. Empty if no local reply body.
   */
  absl::string_view localReplyBody() const;

  /**
   * @return AccessLog::AccessLogType the type of access log. NotSet if this is not used for
   * access logging.
   */
  AccessLog::AccessLogType accessLogType() const;

  static constexpr absl::string_view category() { return "http"; }

private:
  const Http::RequestHeaderMap* request_headers_{};
  const Http::ResponseHeaderMap* response_headers_{};
  const Http::ResponseTrailerMap* response_trailers_{};
  absl::string_view local_reply_body_{};
  AccessLog::AccessLogType log_type_{AccessLog::AccessLogType::NotSet};
};

// Alias of FormatterBase<HttpFormatterContext> for backward compatibility.
using Formatter = FormatterBase<HttpFormatterContext>;
using FormatterPtr = std::unique_ptr<Formatter>;

class FormatterProvider : public FormatterProviderBase<HttpFormatterContext> {
public:
  // FormatterProviderBase<HttpFormatterContext>
  absl::optional<std::string> format(const HttpFormatterContext& context,
                                     const StreamInfo::StreamInfo& info) const override {
    return format(context.requestHeaders(), context.responseHeaders(), context.responseTrailers(),
                  info, context.localReplyBody(), context.accessLogType());
  }
  ProtobufWkt::Value formatValue(const HttpFormatterContext& context,
                                 const StreamInfo::StreamInfo& info) const override {
    return formatValue(context.requestHeaders(), context.responseHeaders(),
                       context.responseTrailers(), info, context.localReplyBody(),
                       context.accessLogType());
  }

private:
  /**
   * Extract a value from the provided headers/trailers/stream.
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param stream_info supplies the stream info.
   * @param local_reply_body supplies the local reply body.
   * @return absl::optional<std::string> optional string containing a single value extracted from
   * the given headers/trailers/stream.
   */
  virtual absl::optional<std::string> format(const Http::RequestHeaderMap& request_headers,
                                             const Http::ResponseHeaderMap& response_headers,
                                             const Http::ResponseTrailerMap& response_trailers,
                                             const StreamInfo::StreamInfo& stream_info,
                                             absl::string_view local_reply_body,
                                             AccessLog::AccessLogType access_log_type) const PURE;
  /**
   * Extract a value from the provided headers/trailers/stream, preserving the value's type.
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param stream_info supplies the stream info.
   * @param local_reply_body supplies the local reply body.
   * @return ProtobufWkt::Value containing a single value extracted from the given
   *         headers/trailers/stream.
   */
  virtual ProtobufWkt::Value formatValue(const Http::RequestHeaderMap& request_headers,
                                         const Http::ResponseHeaderMap& response_headers,
                                         const Http::ResponseTrailerMap& response_trailers,
                                         const StreamInfo::StreamInfo& stream_info,
                                         absl::string_view local_reply_body,
                                         AccessLog::AccessLogType access_log_type) const PURE;
};
using FormatterProviderPtr = std::unique_ptr<FormatterProviderBase<HttpFormatterContext>>;

using CommandParser = CommandParserBase<HttpFormatterContext>;
using CommandParserPtr = std::unique_ptr<CommandParser>;

/**
 * Specialization of CommandParserFactoryBase for HTTP formatter.
 */
template <> class CommandParserFactoryBase<HttpFormatterContext> : public Config::TypedFactory {
public:
  /**
   * Creates a particular CommandParser implementation.
   *
   * @param config supplies the configuration for the command parser.
   * @param context supplies the factory context.
   * @return CommandParserPtr the CommandParser which will be used in
   * SubstitutionFormatParser::parse() when evaluating an access log format string.
   */
  virtual CommandParserPtr
  createCommandParserFromProto(const Protobuf::Message& config,
                               Server::Configuration::CommonFactoryContext& context) PURE;

  // Use "envoy.formatter" as category name of HTTP formatter for backward compatibility.
  std::string category() const override { return "envoy.formatter"; }
};

// Alias of CommandParserFactoryBase<HttpFormatterContext> for backward compatibility.
using CommandParserFactory = CommandParserFactoryBase<HttpFormatterContext>;

} // namespace Formatter
} // namespace Envoy
