#include "source/extensions/http/custom_response/local_response_policy/local_response_policy.h"

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
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace CustomResponse {
LocalResponsePolicy::LocalResponsePolicy(
    const envoy::extensions::http::custom_response::local_response_policy::v3::LocalResponsePolicy&
        config,
    Server::Configuration::ServerFactoryContext& context)
    : local_body_{config.has_body()
                      ? absl::optional<std::string>(THROW_OR_RETURN_VALUE(
                            Config::DataSource::read(config.body(), true, context.api()),
                            std::string))
                      : absl::optional<std::string>{}},
      status_code_{config.has_status_code()
                       ? absl::optional<Envoy::Http::Code>(
                             static_cast<Envoy::Http::Code>(config.status_code().value()))
                       : absl::optional<Envoy::Http::Code>{}},
      header_parser_(THROW_OR_RETURN_VALUE(
          Envoy::Router::HeaderParser::configure(config.response_headers_to_add()),
          Router::HeaderParserPtr)) {

  // TODO(wbpcode): these is a potential bug of message validation. The validation visitor
  // of server context should not be used here directly. But this is bug is not introduced
  // by this PR and will be fixed in the future.
  Server::GenericFactoryContextImpl generic_context(context, context.messageValidationVisitor());
  if (config.has_body_format()) {
    formatter_ =
        THROW_OR_RETURN_VALUE(Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
                                  config.body_format(), generic_context),
                              Formatter::FormatterBasePtr<Formatter::HttpFormatterContext>);
  }
}

// TODO(pradeepcrao): investigate if this code can be made common with
// Envoy::LocalReply::BodyFormatter for consistent behavior.
void LocalResponsePolicy::formatBody(const Envoy::Http::RequestHeaderMap& request_headers,
                                     const Envoy::Http::ResponseHeaderMap& response_headers,
                                     const StreamInfo::StreamInfo& stream_info,
                                     std::string& body) const {
  if (local_body_.has_value()) {
    body = local_body_.value();
  }

  if (formatter_) {
    body = formatter_->formatWithContext({&request_headers, &response_headers, nullptr, body},
                                         stream_info);
  }
}

Envoy::Http::FilterHeadersStatus LocalResponsePolicy::encodeHeaders(
    Envoy::Http::ResponseHeaderMap& headers, bool,
    Extensions::HttpFilters::CustomResponse::CustomResponseFilter& custom_response_filter) const {
  auto encoder_callbacks = custom_response_filter.encoderCallbacks();
  ENVOY_BUG(encoder_callbacks->streamInfo().filterState()->getDataReadOnly<Policy>(
                "envoy.filters.http.custom_response") == nullptr,
            "Filter State should not be set when using the LocalResponse policy.");
  // Handle local body
  std::string body;
  Envoy::Http::Code code = getStatusCodeForLocalReply(headers);
  formatBody(encoder_callbacks->streamInfo().getRequestHeaders() == nullptr
                 ? *Envoy::Http::StaticEmptyHeaders::get().request_headers
                 : *encoder_callbacks->streamInfo().getRequestHeaders(),
             headers, encoder_callbacks->streamInfo(), body);

  const auto mutate_headers = [this, encoder_callbacks](Envoy::Http::ResponseHeaderMap& headers) {
    header_parser_->evaluateHeaders(headers, encoder_callbacks->streamInfo());
  };
  encoder_callbacks->sendLocalReply(code, body, mutate_headers, absl::nullopt, "");
  return Envoy::Http::FilterHeadersStatus::StopIteration;
}

Envoy::Http::Code LocalResponsePolicy::getStatusCodeForLocalReply(
    const Envoy::Http::ResponseHeaderMap& response_headers) const {
  Envoy::Http::Code code = Envoy::Http::Code::InternalServerError;
  if (status_code_.has_value()) {
    code = *status_code_;
  } else if (auto current_code = Envoy::Http::Utility::getResponseStatusOrNullopt(response_headers);
             current_code.has_value()) {
    code = static_cast<Envoy::Http::Code>(*current_code);
  }
  return code;
}
} // namespace CustomResponse
} // namespace Http
} // namespace Extensions
} // namespace Envoy
