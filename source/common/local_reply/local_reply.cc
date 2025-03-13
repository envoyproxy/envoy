#include "source/common/local_reply/local_reply.h"

#include <string>
#include <vector>

#include "envoy/api/api.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/config/datasource.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/router/header_parser.h"

namespace Envoy {
namespace LocalReply {

class BodyFormatter {
public:
  BodyFormatter() : content_type_(Http::Headers::get().ContentTypeValues.Text) {}

  static absl::StatusOr<std::unique_ptr<BodyFormatter>>
  create(const envoy::config::core::v3::SubstitutionFormatString& config,
         Server::Configuration::GenericFactoryContext& context) {
    auto formatter_or_error =
        Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config, context);
    RETURN_IF_NOT_OK_REF(formatter_or_error.status());
    return std::make_unique<BodyFormatter>(config, std::move(*formatter_or_error));
  }

  BodyFormatter(const envoy::config::core::v3::SubstitutionFormatString& config,
                Formatter::FormatterPtr&& formatter)
      : formatter_(std::move(formatter)),
        content_type_(
            !config.content_type().empty() ? config.content_type()
            : config.format_case() ==
                    envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat
                ? Http::Headers::get().ContentTypeValues.Json
                : Http::Headers::get().ContentTypeValues.Text) {}

  void format(const Http::RequestHeaderMap& request_headers,
              const Http::ResponseHeaderMap& response_headers,
              const Http::ResponseTrailerMap& response_trailers,
              const StreamInfo::StreamInfo& stream_info, std::string& body,
              absl::string_view& content_type) const {
    // No specific formatter is provided and the default formatter %LOCAL_REPLY_BODY% will
    // be used. That means the body will be the same as the original body and we don't need
    // to format it.
    if (formatter_ != nullptr) {
      body = formatter_->formatWithContext(
          {&request_headers, &response_headers, &response_trailers, body}, stream_info);
    }
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
  static absl::StatusOr<std::unique_ptr<ResponseMapper>>
  create(const envoy::extensions::filters::network::http_connection_manager::v3::ResponseMapper&
             config,
         Server::Configuration::FactoryContext& context) {
    absl::Status creation_status = absl::OkStatus();
    auto ret = std::make_unique<ResponseMapper>(config, context, creation_status);
    RETURN_IF_NOT_OK(creation_status);
    return ret;
  }

  ResponseMapper(
      const envoy::extensions::filters::network::http_connection_manager::v3::ResponseMapper&
          config,
      Server::Configuration::FactoryContext& context, absl::Status& creation_status)
      : filter_(AccessLog::FilterFactory::fromProto(config.filter(), context)) {
    if (config.has_status_code()) {
      status_code_ = static_cast<Http::Code>(config.status_code().value());
    }
    if (config.has_body()) {
      auto body_or_error =
          Config::DataSource::read(config.body(), true, context.serverFactoryContext().api());
      SET_AND_RETURN_IF_NOT_OK(body_or_error.status(), creation_status);
      body_ = *body_or_error;
    }

    if (config.has_body_format_override()) {
      auto formatter_or_error = BodyFormatter::create(config.body_format_override(), context);
      SET_AND_RETURN_IF_NOT_OK(formatter_or_error.status(), creation_status);
      body_formatter_ = std::move(*formatter_or_error);
    }

    auto parser_or_error = Envoy::Router::HeaderParser::configure(config.headers_to_add());
    SET_AND_RETURN_IF_NOT_OK(parser_or_error.status(), creation_status);
    header_parser_ = std::move(*parser_or_error);
  }

  bool matchAndRewrite(const Http::RequestHeaderMap& request_headers,
                       Http::ResponseHeaderMap& response_headers,
                       const Http::ResponseTrailerMap& response_trailers,
                       StreamInfo::StreamInfo& stream_info, Http::Code& code, std::string& body,
                       BodyFormatter*& final_formatter) const {
    // If not matched, just bail out.
    if (filter_ == nullptr ||
        !filter_->evaluate({&request_headers, &response_headers, &response_trailers},
                           stream_info)) {
      return false;
    }

    if (body_.has_value()) {
      body = body_.value();
    }

    header_parser_->evaluateHeaders(response_headers, {&request_headers, &response_headers},
                                    stream_info);

    if (status_code_.has_value() && code != status_code_.value()) {
      code = status_code_.value();
      response_headers.setStatus(std::to_string(enumToInt(code)));
      stream_info.setResponseCode(static_cast<uint32_t>(code));
    }

    if (body_formatter_) {
      final_formatter = body_formatter_.get();
    }
    return true;
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
  LocalReplyImpl() : body_formatter_(std::make_unique<BodyFormatter>()) {}

  static absl::StatusOr<std::unique_ptr<LocalReplyImpl>>
  create(const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
             config,
         Server::Configuration::FactoryContext& context) {
    absl::Status creation_status = absl::OkStatus();
    auto ret = std::make_unique<LocalReplyImpl>(config, context, creation_status);
    RETURN_IF_NOT_OK(creation_status);
    return ret;
  }

  LocalReplyImpl(
      const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
          config,
      Server::Configuration::FactoryContext& context, absl::Status& creation_status) {
    if (!config.has_body_format()) {
      body_formatter_ = std::make_unique<BodyFormatter>();
    } else {
      auto formatter_or_error = BodyFormatter::create(config.body_format(), context);
      SET_AND_RETURN_IF_NOT_OK(formatter_or_error.status(), creation_status);
      body_formatter_ = std::move(*formatter_or_error);
    }

    for (const auto& mapper : config.mappers()) {
      auto mapper_or_error = ResponseMapper::create(mapper, context);
      SET_AND_RETURN_IF_NOT_OK(mapper_or_error.status(), creation_status);
      mappers_.emplace_back(std::move(*mapper_or_error));
    }
  }

  void rewrite(const Http::RequestHeaderMap* request_headers,
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
    return final_formatter->format(*request_headers, response_headers,
                                   *Http::StaticEmptyHeaders::get().response_trailers, stream_info,
                                   body, content_type);
  }

private:
  std::list<ResponseMapperPtr> mappers_;
  BodyFormatterPtr body_formatter_;
};

LocalReplyPtr Factory::createDefault() { return std::make_unique<LocalReplyImpl>(); }

absl::StatusOr<LocalReplyPtr> Factory::create(
    const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
        config,
    Server::Configuration::FactoryContext& context) {
  return LocalReplyImpl::create(config, context);
}

} // namespace LocalReply
} // namespace Envoy
