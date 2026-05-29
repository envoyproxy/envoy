#include "source/common/http/http_service_headers.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Http {

HttpServiceHeadersApplicator::HttpServiceHeadersApplicator(
    const envoy::config::core::v3::HttpService& http_service,
    Server::Configuration::ServerFactoryContext& server_context, absl::Status& creation_status)
    : stream_info_(server_context.timeSource(), nullptr,
                   StreamInfo::FilterState::LifeSpan::FilterChain) {

  // Formatters can only be instantiated on the main thread because some create thread local
  // storage.
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  Server::GenericFactoryContextImpl generic_context{server_context,
                                                    server_context.messageValidationVisitor()};

  auto commands = Formatter::SubstitutionFormatStringUtils::parseFormatters(
      http_service.formatters(), generic_context);
  SET_AND_RETURN_IF_NOT_OK(commands.status(), creation_status);

  for (const auto& header_value_option : http_service.request_headers_to_add()) {
    const auto& header = header_value_option.header();
    if (!header.value().empty()) {
      auto formatter_or_error = Formatter::FormatterImpl::create(header.value(), false, *commands);
      SET_AND_RETURN_IF_NOT_OK(formatter_or_error.status(), creation_status);
      formatted_headers_.emplace_back(LowerCaseString(header.key()),
                                      std::move(formatter_or_error.value()));
    } else {
      static_headers_.emplace_back(LowerCaseString(header.key()), header.raw_value());
    }
  }
}

std::unique_ptr<HttpServiceHeadersApplicator> HttpServiceHeadersApplicator::createOrThrow(
    const envoy::config::core::v3::HttpService& http_service,
    Server::Configuration::ServerFactoryContext& server_context) {
  absl::Status creation_status;
  auto applicator =
      std::make_unique<HttpServiceHeadersApplicator>(http_service, server_context, creation_status);
  THROW_IF_NOT_OK_REF(creation_status);
  return applicator;
}

void HttpServiceHeadersApplicator::apply(RequestHeaderMap& headers) const {
  for (const auto& header_pair : static_headers_) {
    headers.setReference(header_pair.first, header_pair.second);
  }
  if (!formatted_headers_.empty()) {
    for (const auto& header_pair : formatted_headers_) {
      headers.setCopy(header_pair.first, header_pair.second->format({}, stream_info_));
    }
  }
}

} // namespace Http
} // namespace Envoy
