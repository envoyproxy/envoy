#include "common/router/resp_header_parser.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Router {

ResponseHeaderParserPtr ResponseHeaderParser::parse(
    const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers_to_add,
    const Protobuf::RepeatedPtrField<ProtobufTypes::String>& headers_to_remove) {
  ResponseHeaderParserPtr response_header_parser(new ResponseHeaderParser());

  for (const auto& header_value_option : headers_to_add) {
    response_header_parser->headers_to_add_.emplace_back(
        HeaderAddition{Http::LowerCaseString(header_value_option.header().key()),
                       header_value_option.header().value(),
                       PROTOBUF_GET_WRAPPED_OR_DEFAULT(header_value_option, append, true)});
  }

  for (const std::string& header : headers_to_remove) {
    response_header_parser->headers_to_remove_.emplace_back(Http::LowerCaseString(header));
  }

  return response_header_parser;
}

// TODO(zuercher): If modifying this function to perform header substitutions, consider refactoring
// this class and RequestHeaderParser to share overlapping code.
void ResponseHeaderParser::evaluateResponseHeaders(Http::HeaderMap& headers) const {
  for (const auto& header_value : headers_to_add_) {
    if (header_value.append_) {
      headers.addReferenceKey(header_value.header_, header_value.value_);
    } else {
      headers.setReferenceKey(header_value.header_, header_value.value_);
    }
  }

  for (const auto& header : headers_to_remove_) {
    headers.remove(header);
  }
}

} // namespace Router
} // namespace Envoy
