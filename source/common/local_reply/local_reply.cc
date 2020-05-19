#include "common/local_reply/local_reply.h"

#include <string>
#include <vector>

#include "common/access_log/access_log_formatter.h"
#include "common/access_log/access_log_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/substitution_format_string.h"
#include "common/config/datasource.h"
#include "common/http/header_map_impl.h"

namespace Envoy {
namespace LocalReply {

class BodyFormatter {
public:
  BodyFormatter() { formatter_ = std::make_unique<Envoy::AccessLog::FormatterImpl>("%RESP_BODY%"); }

  BodyFormatter(const envoy::config::core::v3::SubstitutionFormatString& config) {
    formatter_ = SubstitutionFormatStringUtils::fromProtoConfig(config);
    if (config.format_case() ==
        envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat) {
      content_type_ = Http::Headers::get().ContentTypeValues.Json;
    }
  }

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
  AccessLog::FormatterPtr formatter_;
  absl::string_view content_type_{Http::Headers::get().ContentTypeValues.Text};
};

using BodyFormatterPtr = std::unique_ptr<BodyFormatter>;

class ResponseMapper {
public:
  ResponseMapper(
      const envoy::extensions::filters::network::http_connection_manager::v3::ResponseMapper&
          config,
      Server::Configuration::FactoryContext& context) {
    filter_ = AccessLog::FilterFactory::fromProto(
        config.filter(), context.runtime(), context.random(), context.messageValidationVisitor());

    if (config.has_status_code()) {
      status_code_ = static_cast<Http::Code>(config.status_code().value());
    }
    if (config.has_body()) {
      body_ = Config::DataSource::read(config.body(), true, context.api());
    }

    if (config.has_body_format()) {
      body_formatter_ = std::make_unique<BodyFormatter>(config.body_format());
    }
  }

  bool matchAndRewrite(const Http::RequestHeaderMap& request_headers,
                       Http::ResponseHeaderMap& response_headers,
                       const Http::ResponseTrailerMap& response_trailers,
                       StreamInfo::StreamInfoImpl& stream_info, Http::Code& code, std::string& body,
                       BodyFormatter*& final_formatter) const {
    // If not matched, just bail out.
    if (!filter_->evaluate(stream_info, request_headers, response_headers, response_trailers)) {
      return false;
    }

    if (body_.has_value()) {
      body = body_.value();
    }

    if (status_code_.has_value() && code != status_code_.value()) {
      code = status_code_.value();
      response_headers.setReferenceStatus(std::to_string(enumToInt(code)));
      stream_info.response_code_ = static_cast<uint32_t>(code);
    }

    if (body_formatter_) {
      final_formatter = body_formatter_.get();
    }
    return true;
  }

private:
  AccessLog::FilterPtr filter_;
  absl::optional<Http::Code> status_code_;
  absl::optional<std::string> body_;
  BodyFormatterPtr body_formatter_;
};

using ResponseMapperPtr = std::unique_ptr<ResponseMapper>;

class LocalReplyImpl : public LocalReply {
public:
  LocalReplyImpl(
      const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
          config,
      Server::Configuration::FactoryContext& context) {
    for (const auto& mapper : config.mappers()) {
      mappers_.emplace_back(std::make_unique<ResponseMapper>(mapper, context));
    }

    if (config.has_body_format()) {
      body_formatter_ = std::make_unique<BodyFormatter>(config.body_format());
    } else {
      body_formatter_ = std::make_unique<BodyFormatter>();
    }
  }

  void rewrite(const Http::RequestHeaderMap* request_headers,
               StreamInfo::StreamInfoImpl& stream_info, Http::Code& code, std::string& body,
               absl::string_view& content_type) const override {
    Http::ResponseTrailerMapPtr response_trailers{
        Http::createHeaderMap<Http::ResponseTrailerMapImpl>({})};

    Http::RequestHeaderMapPtr empty_request_headers;
    if (request_headers == nullptr) {
      empty_request_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>({});
      request_headers = empty_request_headers.get();
    }

    Http::ResponseHeaderMapPtr response_headers{Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
        {{Http::Headers::get().Status, std::to_string(enumToInt(code))}})};

    BodyFormatter* final_formatter{};
    for (const auto& mapper : mappers_) {
      if (mapper->matchAndRewrite(*request_headers, *response_headers, *response_trailers,
                                  stream_info, code, body, final_formatter)) {
        break;
      }
    }

    if (!final_formatter) {
      final_formatter = body_formatter_.get();
    }
    return final_formatter->format(*request_headers, *response_headers, *response_trailers,
                                   stream_info, body, content_type);
  }

private:
  std::list<ResponseMapperPtr> mappers_;
  BodyFormatterPtr body_formatter_;
};

LocalReplyPtr Factory::create(
    const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
        config,
    Server::Configuration::FactoryContext& context) {
  return std::make_unique<LocalReplyImpl>(config, context);
}

} // namespace LocalReply
} // namespace Envoy
