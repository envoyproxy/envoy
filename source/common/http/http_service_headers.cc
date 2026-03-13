#include "source/common/http/http_service_headers.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Http {

HttpServiceHeadersApplicator::HttpServiceHeadersApplicator(
    const envoy::config::core::v3::HttpService& http_service,
    Server::Configuration::ServerFactoryContext& server_context, absl::Status& creation_status)
    : time_source_(server_context.timeSource()) {
  Server::GenericFactoryContextImpl generic_context{server_context,
                                                    server_context.messageValidationVisitor()};
  for (const auto& header_value_option : http_service.request_headers_to_add()) {
    const auto& header = header_value_option.header();
    const uint8_t fields_set = (header.has_formatted_value() ? 1 : 0) +
                               (!header.raw_value().empty() ? 1 : 0) +
                               (!header.value().empty() ? 1 : 0);
    if (fields_set > 1) {
      creation_status = absl::InvalidArgumentError(
          fmt::format("header '{}': only one of 'value', 'raw_value', or 'formatted_value' can be "
                      "set",
                      header.key()));
      return;
    }
    if (header.has_formatted_value()) {
      auto formatter_or_error = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
          header.formatted_value(), generic_context);
      SET_AND_RETURN_IF_NOT_OK(formatter_or_error.status(), creation_status);
      formatted_headers_.emplace_back(LowerCaseString(header.key()),
                                      std::move(formatter_or_error.value()));
    } else if (!header.raw_value().empty()) {
      static_headers_.emplace_back(LowerCaseString(header.key()), header.raw_value());
    } else {
      static_headers_.emplace_back(LowerCaseString(header.key()), header.value());
    }
  }
}

void HttpServiceHeadersApplicator::apply(RequestHeaderMap& headers) const {
  for (const auto& header_pair : static_headers_) {
    headers.setReference(header_pair.first, header_pair.second);
  }
  if (!formatted_headers_.empty()) {
    // A `StreamInfo` is required, but in this context we don't have one, so create an empty one.
    // This allows formatters that don't require any stream info to succeed, such as extensions that
    // load data externally for API keys and similar.
    StreamInfo::StreamInfoImpl stream_info{time_source_, nullptr,
                                           StreamInfo::FilterState::LifeSpan::FilterChain};
    for (const auto& header_pair : formatted_headers_) {
      headers.setCopy(header_pair.first, header_pair.second->format({}, stream_info));
    }
  }
}

} // namespace Http
} // namespace Envoy
