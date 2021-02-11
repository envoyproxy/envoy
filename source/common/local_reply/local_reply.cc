#include "common/local_reply/local_reply.h"

#include <string>
#include <vector>

#include "envoy/api/api.h"

#include "common/access_log/access_log_impl.h"
#include "common/common/enum_to_int.h"
#include "common/config/datasource.h"
#include "common/formatter/substitution_format_string.h"
#include "common/formatter/substitution_formatter.h"
#include "common/http/header_map_impl.h"
#include "common/http/utility.h"
#include "common/router/header_parser.h"

namespace Envoy {
namespace LocalReply {

class BodyFormatter {
public:
  BodyFormatter()
      : formatter_(std::make_unique<Envoy::Formatter::FormatterImpl>("%LOCAL_REPLY_BODY%")),
        content_type_(Http::Headers::get().ContentTypeValues.Text) {}

  BodyFormatter(const envoy::config::core::v3::SubstitutionFormatString& config, Api::Api& api)
      : formatter_(Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config, api)),
        content_type_(
            !config.content_type().empty()
                ? config.content_type()
                : config.format_case() ==
                          envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat
                      ? Http::Headers::get().ContentTypeValues.Json
                      : Http::Headers::get().ContentTypeValues.Text) {}

  void format(const Http::RequestHeaderMap& request_headers,
              const Http::ResponseHeaderMap& response_headers,
              const Http::ResponseTrailerMap& response_trailers,
              const StreamInfo::StreamInfo& stream_info, std::string& body,
              absl::string_view& content_type) const {
    body =
        formatter_->format(request_headers, response_headers, response_trailers, stream_info, body);
    content_type = content_type_;
  }

private:
  const Formatter::FormatterPtr formatter_;
  const std::string content_type_;
};

using BodyFormatterPtr = std::unique_ptr<BodyFormatter>;
using HeaderParserPtr = std::unique_ptr<Envoy::Router::HeaderParser>;

class ResponseMapper {
public:
  ResponseMapper(
      const envoy::extensions::filters::network::http_connection_manager::v3::ResponseMapper&
          config,
      Server::Configuration::FactoryContext& context)
      : filter_(AccessLog::FilterFactory::fromProto(config.filter(), context.runtime(),
                                                    context.api().randomGenerator(),
                                                    context.messageValidationVisitor())) {
    if (config.has_status_code()) {
      status_code_ = static_cast<Http::Code>(config.status_code().value());
    }
    if (config.has_body()) {
      body_ = Config::DataSource::read(config.body(), true, context.api());
    }

    if (config.has_body_format_override()) {
      body_formatter_ =
          std::make_unique<BodyFormatter>(config.body_format_override(), context.api());
    }

    header_parser_ = Envoy::Router::HeaderParser::configure(config.headers_to_add());
  }

  bool matchAndRewrite(const Http::RequestHeaderMap& request_headers,
                       Http::ResponseHeaderMap& response_headers,
                       const Http::ResponseTrailerMap& response_trailers,
                       StreamInfo::StreamInfo& stream_info, Http::Code& code, std::string& body,
                       BodyFormatter*& final_formatter) const {
    // If not matched, just bail out.
    if (!match(&request_headers, response_headers, &response_trailers, stream_info)) {
      return false;
    }

    rewrite(request_headers, response_headers, stream_info, code, body, final_formatter);
    return true;
  }

  // Decide if a request/response pair matches this mapper.
  bool match(const Http::RequestHeaderMap* request_headers,
             const Http::ResponseHeaderMap& response_headers,
             const Http::ResponseTrailerMap* response_trailers,
             StreamInfo::StreamInfo& stream_info) const {
    // Set response code on the stream_info because it's used by the StatusCode filter.
    // Further, we know that the status header present on the upstream response headers
    // is the status we want to match on. It may not be the status we send downstream
    // to the client, though, because we may call `rewrite` later.
    //
    // Under normal circumstances we should have a response status by this point, because
    // either the upstream set it or the router filter set it. If for whatever reason we
    // don't, skip setting the stream info's response code and just let our evaluation
    // logic do without it. We can't do much better, and we certainly don't want to throw
    // an exception and crash here.
    if (response_headers.Status() != nullptr) {
      stream_info.setResponseCode(
          static_cast<uint32_t>(Http::Utility::getResponseStatus(response_headers)));
    }

    if (request_headers == nullptr) {
      request_headers = Http::StaticEmptyHeaders::get().request_headers.get();
    }

    if (response_trailers == nullptr) {
      response_trailers = Http::StaticEmptyHeaders::get().response_trailers.get();
    }

    return filter_->evaluate(stream_info, *request_headers, response_headers, *response_trailers);
  }

  void rewrite(const Http::RequestHeaderMap&, Http::ResponseHeaderMap& response_headers,
               StreamInfo::StreamInfo& stream_info, Http::Code& code, std::string& body,
               BodyFormatter*& final_formatter) const {
    if (body_.has_value()) {
      body = body_.value();
    }

    header_parser_->evaluateHeaders(response_headers, stream_info);

    if (status_code_.has_value() && code != status_code_.value()) {
      code = status_code_.value();
      response_headers.setStatus(std::to_string(enumToInt(code)));
      stream_info.setResponseCode(static_cast<uint32_t>(code));
    }

    if (body_formatter_) {
      final_formatter = body_formatter_.get();
    }
  }

private:
  const AccessLog::FilterPtr filter_;
  absl::optional<Http::Code> status_code_;
  absl::optional<std::string> body_;
  HeaderParserPtr header_parser_;
  BodyFormatterPtr body_formatter_;
};

using ResponseMapperPtr = std::unique_ptr<ResponseMapper>;

class LocalReplyImpl : public LocalReply {
public:
  LocalReplyImpl()
      : body_formatter_(std::make_unique<BodyFormatter>()), has_configured_body_formatter_(false) {}

  LocalReplyImpl(
      const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
          config,
      Server::Configuration::FactoryContext& context)
      : body_formatter_(config.has_body_format()
                            ? std::make_unique<BodyFormatter>(config.body_format(), context.api())
                            : std::make_unique<BodyFormatter>()),
        has_configured_body_formatter_(config.has_body_format()) {
    for (const auto& mapper : config.mappers()) {
      mappers_.emplace_back(std::make_unique<ResponseMapper>(mapper, context));
    }
  }

  bool matchesAnyMapper(const Http::RequestHeaderMap* request_headers,
                        const Http::ResponseHeaderMap& response_headers,
                        const Http::ResponseTrailerMap* response_trailers,
                        StreamInfo::StreamInfo& stream_info) const override {
    for (const auto& mapper : mappers_) {
      if (mapper->match(request_headers, response_headers, response_trailers, stream_info)) {
        return true;
      }
    }
    return false;
  }

  bool rewrite(const Http::RequestHeaderMap* request_headers,
               Http::ResponseHeaderMap& response_headers, StreamInfo::StreamInfo& stream_info,
               Http::Code& code, std::string& body,
               absl::string_view& content_type) const override {
    // Set response code to stream_info and response_headers due to:
    // 1) StatusCode filter is using response_code from stream_info,
    // 2) %RESP(:status)% is from Status() in response_headers.
    response_headers.setStatus(std::to_string(enumToInt(code)));
    stream_info.setResponseCode(static_cast<uint32_t>(code));

    if (request_headers == nullptr) {
      request_headers = Http::StaticEmptyHeaders::get().request_headers.get();
    }

    BodyFormatter* final_formatter{};
    for (const auto& mapper : mappers_) {
      if (mapper->matchAndRewrite(*request_headers, response_headers,
                                  *Http::StaticEmptyHeaders::get().response_trailers, stream_info,
                                  code, body, final_formatter)) {
        break;
      }
    }

    if (!final_formatter) {
      final_formatter = body_formatter_.get();
    }
    final_formatter->format(*request_headers, response_headers,
                            *Http::StaticEmptyHeaders::get().response_trailers, stream_info, body,
                            content_type);
    // If this local reply has a configured body formatter or the final formatter is not the
    // default formatter, then we know `body` was modified by an explicitly configured formatter.
    return has_configured_body_formatter_ || final_formatter != body_formatter_.get();
  }

private:
  std::list<ResponseMapperPtr> mappers_;
  const BodyFormatterPtr body_formatter_;
  bool has_configured_body_formatter_;
};

LocalReplyPtr Factory::createDefault() { return std::make_unique<LocalReplyImpl>(); }

LocalReplyPtr Factory::create(
    const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
        config,
    Server::Configuration::FactoryContext& context) {
  return std::make_unique<LocalReplyImpl>(config, context);
}

} // namespace LocalReply
} // namespace Envoy
