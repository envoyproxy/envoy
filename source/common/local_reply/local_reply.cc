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

class ResponseRewriter {
public:
  ResponseRewriter(
      const envoy::extensions::filters::network::http_connection_manager::v3::ResponseRewriter&
          config,
      Api::Api& api) {
    if (config.has_status_code()) {
      status_code_ = static_cast<Http::Code>(config.status_code().value());
    }
    if (config.has_body()) {
      body_ = Config::DataSource::read(config.body(), true, api);
    }
  }

  void rewrite(Http::Code& code, std::string& body) const {
    if (status_code_.has_value()) {
      code = status_code_.value();
    }
    if (body_.has_value()) {
      body = body_.value();
    }
  }

private:
  absl::optional<Http::Code> status_code_;
  absl::optional<std::string> body_;
};

using ResponseRewriterPtr = std::unique_ptr<ResponseRewriter>;

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

/**
 * Configuration of response mapper which contains pair of filter and definition of rewriter.
 */
class ResponseMapper {
public:
  ResponseMapper(
      const envoy::extensions::filters::network::http_connection_manager::v3::ResponseMapper&
          config,
      Server::Configuration::FactoryContext& context) {
    filter_ = AccessLog::FilterFactory::fromProto(
        config.filter(), context.runtime(), context.random(), context.messageValidationVisitor());

    if (config.has_rewriter()) {
      rewriter_ = std::make_unique<ResponseRewriter>(config.rewriter(), context.api());
    }

    if (config.has_format()) {
      formatter_ = std::make_unique<BodyFormatter>(config.format());
    }
  }

  AccessLog::FilterPtr filter_;
  ResponseRewriterPtr rewriter_;
  BodyFormatterPtr formatter_;
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

    if (config.has_format()) {
      formatter_ = std::make_unique<BodyFormatter>(config.format());
    } else {
      formatter_ = std::make_unique<BodyFormatter>();
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
      if (mapper->filter_->evaluate(stream_info, *request_headers, *response_headers,
                                    *response_trailers)) {
        if (mapper->rewriter_) {
          Http::Code old_code = code;
          mapper->rewriter_->rewrite(code, body);
          if (code != old_code) {
            response_headers->setReferenceStatus(std::to_string(enumToInt(code)));
            stream_info.response_code_ = static_cast<uint32_t>(code);
          }
        }

        if (mapper->formatter_) {
          final_formatter = mapper->formatter_.get();
        }
        break;
      }
    }

    if (!final_formatter) {
      final_formatter = formatter_.get();
    }
    return final_formatter->format(*request_headers, *response_headers, *response_trailers,
                                   stream_info, body, content_type);
  }

private:
  std::list<ResponseMapperPtr> mappers_;
  BodyFormatterPtr formatter_;
};

LocalReplyPtr Factory::create(
    const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
        config,
    Server::Configuration::FactoryContext& context) {
  return std::make_unique<LocalReplyImpl>(config, context);
}

} // namespace LocalReply
} // namespace Envoy
