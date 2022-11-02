#include "source/extensions/filters/http/custom_response/policies/local_response_policy.h"

#include "envoy/stream_info/filter_state.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/config/datasource.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/router/header_parser.h"
#include "source/extensions/filters/http/custom_response/custom_response_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {
LocalResponsePolicy::LocalResponsePolicy(
    const envoy::extensions::filters::http::custom_response::v3::LocalResponsePolicy& config,
    Server::Configuration::CommonFactoryContext& context)
    : formatter_(config.has_body_format()
                     ? Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
                           config.body_format(), context)
                     : nullptr) {
  // TODO: is true right here?
  if (config.has_body()) {
    local_body_.emplace(Config::DataSource::read(config.body(), true, context.api()));
  }

  if (config.has_status_code()) {
    status_code_ = static_cast<Http::Code>(config.status_code().value());
  }
  header_parser_ = Envoy::Router::HeaderParser::configure(config.response_headers_to_add());
}

void LocalResponsePolicy::formatBody(const Http::RequestHeaderMap& request_headers,
                                     const Http::ResponseHeaderMap& response_headers,
                                     const StreamInfo::StreamInfo& stream_info,
                                     std::string& body) const {

  if (local_body_.has_value()) {
    body = local_body_.value();
  }

  if (formatter_) {
    formatter_->format(request_headers, response_headers,
                       *Http::StaticEmptyHeaders::get().response_trailers, stream_info, body);
  }
}

Http::FilterHeadersStatus
LocalResponsePolicy::encodeHeaders(Http::ResponseHeaderMap& headers, bool,
                                   CustomResponseFilter& custom_response_filter) const {
  auto encoder_callbacks = custom_response_filter.encoderCallbacks();
  ENVOY_BUG(encoder_callbacks->streamInfo().filterState()->getDataReadOnly<Policy>(
                "envoy.filters.http.custom_response") == nullptr,
            "Filter State should not be set when using the LocalResponse policy.");
  // Handle local body
  std::string body;
  Http::Code code = getStatusCodeForLocalReply(headers);
  if (encoder_callbacks->streamInfo().getRequestHeaders() != nullptr) {
    formatBody(*encoder_callbacks->streamInfo().getRequestHeaders(), headers,
               encoder_callbacks->streamInfo(), body);
  }

  const auto mutate_headers = [this, encoder_callbacks](Http::ResponseHeaderMap& headers) {
    header_parser_->evaluateHeaders(headers, encoder_callbacks->streamInfo());
  };
  encoder_callbacks->sendLocalReply(code, body, mutate_headers, absl::nullopt, "");
  return Http::FilterHeadersStatus::StopIteration;
}

Http::Code LocalResponsePolicy::getStatusCodeForLocalReply(
    const Http::ResponseHeaderMap& response_headers) const {
  Http::Code code = Http::Code::InternalServerError;
  if (status_code_.has_value()) {
    code = *status_code_;
  } else if (auto current_code = Http::Utility::getResponseStatusOrNullopt(response_headers);
             current_code.has_value()) {
    code = static_cast<Http::Code>(*current_code);
  }
  return code;
}
} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
