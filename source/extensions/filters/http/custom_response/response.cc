#include "source/extensions/filters/http/custom_response/response.h"

#include <functional>
#include <string>

#include "envoy/api/api.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/http/async_client.h"
#include "envoy/http/header_map.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/config/datasource.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/router/header_parser.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

Response::Response(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse::Response& config,
    Server::Configuration::CommonFactoryContext& context)
    : name_(config.name()),
      formatter_(config.has_body_format()
                     ? Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
                           config.body_format(), context)
                     : nullptr),
      content_type_(config.has_body_format()
                        ? absl::optional<std::string>(
                              (!config.body_format().content_type().empty()
                                   ? config.body_format().content_type()
                                   : (config.body_format().format_case() ==
                                              envoy::config::core::v3::SubstitutionFormatString::
                                                  FormatCase::kJsonFormat
                                          ? Http::Headers::get().ContentTypeValues.Json
                                          : Http::Headers::get().ContentTypeValues.Text)))
                        : absl::optional<std::string>()) {
  if (config.has_status_code()) {
    status_code_ = static_cast<Http::Code>(config.status_code().value());
  }
  // local data
  if (config.has_local()) {
    // TODO: is true right here?
    local_body_.emplace(Config::DataSource::read(config.local(), true, context.api()));
  } else if (config.has_remote()) {
    remote_data_source_.emplace(config.remote());
  }

  header_parser_ = Envoy::Router::HeaderParser::configure(config.headers_to_add());
}

void Response::rewrite(Http::ResponseHeaderMap& response_headers,
                       StreamInfo::StreamInfo& stream_info, std::string& body, Http::Code& code) {

  if (local_body_.has_value()) {
    body = local_body_.value();
  }

  header_parser_->evaluateHeaders(response_headers, stream_info);

  if (status_code_.has_value()) {
    code = *status_code_;
  } else if (auto current_code = Http::Utility::getResponseStatusOrNullopt(response_headers);
             current_code.has_value()) {
    code = static_cast<Http::Code>(*current_code);
  } else {
    code = Http::Code::InternalServerError;
  }

  if (formatter_) {
    const auto request_headers = Http::StaticEmptyHeaders::get().request_headers.get();
    formatter_->format(*request_headers, response_headers,
                       // TODO: do we need the actual trailers here?
                       *Http::StaticEmptyHeaders::get().response_trailers, stream_info, body);
  }
}

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
