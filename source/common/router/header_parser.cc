#include "common/router/header_parser.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Router {

namespace {

HeaderFormatterPtr parseInternal(const envoy::api::v2::HeaderValueOption& header_value_option) {
  const std::string& format = header_value_option.header().value();
  const bool append = PROTOBUF_GET_WRAPPED_OR_DEFAULT(header_value_option, append, true);

  if (format.find("%") == 0) {
    const size_t last_occ_pos = format.rfind("%");
    if (last_occ_pos == std::string::npos || last_occ_pos <= 1) {
      throw EnvoyException(fmt::format("Incorrect header configuration. Expected variable format "
                                       "%<variable_name>%, actual format {}",
                                       format));
    }
    return HeaderFormatterPtr{
        new RequestInfoHeaderFormatter(format.substr(1, last_occ_pos - 1), append)};
  } else {
    return HeaderFormatterPtr{new PlainHeaderFormatter(format, append)};
  }
}

} // namespace

HeaderParserPtr HeaderParser::configure(
    const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers_to_add) {
  HeaderParserPtr header_parser(new HeaderParser());

  for (const auto& header_value_option : headers_to_add) {
    HeaderFormatterPtr header_formatter = parseInternal(header_value_option);

    header_parser->headers_to_add_.emplace_back(
        Http::LowerCaseString(header_value_option.header().key()), std::move(header_formatter));
  }

  return header_parser;
}

HeaderParserPtr HeaderParser::configure(
    const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers_to_add,
    const Protobuf::RepeatedPtrField<ProtobufTypes::String>& headers_to_remove) {
  HeaderParserPtr header_parser = configure(headers_to_add);

  for (const auto& header : headers_to_remove) {
    header_parser->headers_to_remove_.emplace_back(header);
  }

  return header_parser;
}

void HeaderParser::evaluateHeaders(Http::HeaderMap& headers,
                                   const RequestInfo::RequestInfo& request_info) const {
  for (const auto& formatter : headers_to_add_) {
    const std::string value = formatter.second->format(request_info);
    if (!value.empty()) {
      if (formatter.second->append()) {
        headers.addReferenceKey(formatter.first, value);
      } else {
        headers.setReferenceKey(formatter.first, value);
      }
    }
  }

  for (const auto& header : headers_to_remove_) {
    headers.remove(header);
  }
}

} // namespace Router
} // namespace Envoy
