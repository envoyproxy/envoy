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
      formatter_(config.has_local() && config.local().has_body_format()
                     ? Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
                           config.local().body_format(), context)
                     : nullptr) {
  if (config.has_status_code()) {
    status_code_ = static_cast<Http::Code>(config.status_code().value());
  }
  // local data
  if (config.has_local()) {
    // TODO: is true right here?
    if (config.local().has_body()) {
      local_body_.emplace(Config::DataSource::read(config.local().body(), true, context.api()));
    }
  } else if (config.has_remote()) {
    remote_data_source_.emplace(config.remote());
  }

  header_parser_ = Envoy::Router::HeaderParser::configure(config.headers_to_add());
}

void Response::mutateHeaders(Http::ResponseHeaderMap& response_headers,
                             StreamInfo::StreamInfo& stream_info) const {
  header_parser_->evaluateHeaders(response_headers, stream_info);
}

Http::Code
Response::getStatusCodeForLocalReply(const Http::ResponseHeaderMap& response_headers) const {
  Http::Code code = Http::Code::InternalServerError;
  if (status_code_.has_value()) {
    code = *status_code_;
  } else if (auto current_code = Http::Utility::getResponseStatusOrNullopt(response_headers);
             current_code.has_value()) {
    code = static_cast<Http::Code>(*current_code);
  }
  return code;
}

void Response::formatBody(Http::RequestHeaderMap& request_headers,
                          Http::ResponseHeaderMap& response_headers,
                          StreamInfo::StreamInfo& stream_info, std::string& body) const {

  if (local_body_.has_value()) {
    body = local_body_.value();
  }

  if (formatter_) {
    formatter_->format(request_headers, response_headers,
                       *Http::StaticEmptyHeaders::get().response_trailers, stream_info, body);
  }
}

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
