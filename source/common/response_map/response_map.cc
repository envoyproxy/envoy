#include "common/response_map/response_map.h"

#include <string>
#include <vector>

#include "common/access_log/access_log_impl.h"
#include "common/common/enum_to_int.h"
#include "common/config/datasource.h"
#include "common/formatter/substitution_format_string.h"
#include "common/formatter/substitution_formatter.h"
#include "common/http/header_map_impl.h"
#include "common/http/utility.h"

#include "envoy/api/api.h"

namespace Envoy {
namespace ResponseMap {

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

class ResponseMapper {
public:
  ResponseMapper(
      const envoy::extensions::filters::http::response_map::v3::ResponseMapper& config,
      Server::Configuration::CommonFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validationVisitor)
      : filter_(AccessLog::FilterFactory::fromProto(config.filter(), context.runtime(),
                                                    context.random(),
                                                    validationVisitor)) {
    if (config.has_status_code()) {
      status_code_ = static_cast<Http::Code>(config.status_code().value());
    }
    if (config.has_body()) {
      body_ = Config::DataSource::read(config.body(), true, context.api());
    }

    if (config.has_body_format_override()) {
      body_formatter_ = std::make_unique<BodyFormatter>(config.body_format_override(), context.api());
    }
  }

  // Decide if a request/response pair matches this mapper.
  bool match(const Http::RequestHeaderMap* request_headers,
             const Http::ResponseHeaderMap& response_headers,
             StreamInfo::StreamInfo& stream_info) const {
    // Set response code on the stream_info because it's used by the StatusCode filter.
    // Further, we know that the status header present on the upstream response headers
    // is the status we want to match on. It may not be the status we send downstream
    // to the client, though, because of rewrites below.
    //
    // Under normal circumstances we should have a response status by this point, because
    // either the upstream set it or the router filter set it. If for whatever reason we
    // don't, skip setting the stream info's response code and just let our evaluation
    // logic do without it. We can't do much better, and we certaily don't want to throw
    // an exception and crash here.
    if (response_headers.Status() != nullptr) {
      stream_info.setResponseCode(
          static_cast<uint32_t>(Http::Utility::getResponseStatus(response_headers)));
    }

    if (request_headers == nullptr) {
      request_headers = Http::StaticEmptyHeaders::get().request_headers.get();
    }

    return filter_->evaluate(stream_info,
                             *request_headers,
                             response_headers,
                             *Http::StaticEmptyHeaders::get().response_trailers
                             );
  }

  bool rewrite(const Http::RequestHeaderMap&,
               Http::ResponseHeaderMap& response_headers,
               const Http::ResponseTrailerMap&,
               StreamInfo::StreamInfo&, std::string& body,
               BodyFormatter*& final_formatter) const {
    if (body_.has_value()) {
      body = body_.value();
    }

    if (status_code_.has_value() &&
        Http::Utility::getResponseStatus(response_headers) != enumToInt(status_code_.value())) {
      response_headers.setStatus(std::to_string(enumToInt(status_code_.value())));
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
  BodyFormatterPtr body_formatter_;
};

using ResponseMapperPtr = std::unique_ptr<ResponseMapper>;

class ResponseMapImpl : public ResponseMap {
public:
  ResponseMapImpl() : body_formatter_(std::make_unique<BodyFormatter>()) {}

  ResponseMapImpl(
      const envoy::extensions::filters::http::response_map::v3::ResponseMap& config,
      Server::Configuration::CommonFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validationVisitor)
      : body_formatter_(config.has_body_format()
                            ? std::make_unique<BodyFormatter>(config.body_format(), context.api())
                            : std::make_unique<BodyFormatter>()) {
    for (const auto& mapper : config.mappers()) {
      mappers_.emplace_back(std::make_unique<ResponseMapper>(mapper, context, validationVisitor));
    }
  }

  bool match(const Http::RequestHeaderMap* request_headers,
             const Http::ResponseHeaderMap& response_headers,
             StreamInfo::StreamInfo& stream_info) const override {
    for (const auto& mapper : mappers_) {
      if (mapper->match(request_headers, response_headers, stream_info)) {
        return true;
      }
    }
    return false;
  }

  void rewrite(const Http::RequestHeaderMap* request_headers,
               Http::ResponseHeaderMap& response_headers,
               StreamInfo::StreamInfo& stream_info,
               std::string& body,
               absl::string_view& content_type) const override {
    if (request_headers == nullptr) {
      request_headers = Http::StaticEmptyHeaders::get().request_headers.get();
    }

    BodyFormatter* final_formatter{};
    for (const auto& mapper : mappers_) {
      if (!mapper->match(request_headers, response_headers, stream_info)) {
          continue;
      }

      if (mapper->rewrite(*request_headers, response_headers,
                          *Http::StaticEmptyHeaders::get().response_trailers,
                          stream_info,
                          body, final_formatter)) {
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
  const BodyFormatterPtr body_formatter_;
};

ResponseMapPtr Factory::createDefault() { return std::make_unique<ResponseMapImpl>(); }

ResponseMapPtr Factory::create(
    const envoy::extensions::filters::http::response_map::v3::ResponseMap& config,
    Server::Configuration::CommonFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validationVisitor) {
  return std::make_unique<ResponseMapImpl>(config, context, validationVisitor);
}

} // namespace ResponseMap
} // namespace Envoy
