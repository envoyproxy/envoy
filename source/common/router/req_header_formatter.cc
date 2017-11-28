#include "common/router/req_header_formatter.h"

#include <cstdint>
#include <string>

#include "common/config/rds_json.h"
#include "common/protobuf/utility.h"

#include "fmt/format.h"

namespace Envoy {
namespace Router {

HeaderFormatterPtr RequestHeaderParser::parseInternal(const std::string& format,
                                                      const bool append) {
  if (format.find("%") == 0) {
    const size_t last_occ_pos = format.rfind("%");
    if (last_occ_pos == std::string::npos || last_occ_pos <= 1) {
      throw EnvoyException(fmt::format("Incorrect header configuration. Expected variable format "
                                       "%<variable_name>%, actual format {}",
                                       format));
    }
    return HeaderFormatterPtr{
        new RequestHeaderFormatter(format.substr(1, last_occ_pos - 1), append)};
  } else {
    return HeaderFormatterPtr{new PlainHeaderFormatter(format, append)};
  }
}

RequestHeaderParserPtr RequestHeaderParser::parse(
    const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers) {
  RequestHeaderParserPtr request_header_parser(new RequestHeaderParser());
  for (const auto& header_value_option : headers) {
    HeaderFormatterPtr header_formatter = RequestHeaderParser::parseInternal(
        header_value_option.header().value(),
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(header_value_option, append, true));
    request_header_parser->header_formatters_.push_back(
        {Http::LowerCaseString(header_value_option.header().key()), std::move(header_formatter)});
  }
  return request_header_parser;
}

void RequestHeaderParser::evaluateRequestHeaders(Http::HeaderMap& headers,
                                                 const AccessLog::RequestInfo& request_info) const {
  for (const auto& formatter : header_formatters_) {
    if (formatter.second->append()) {
      headers.addReferenceKey(formatter.first, formatter.second->format(request_info));
    } else {
      headers.setReferenceKey(formatter.first, formatter.second->format(request_info));
    }
  }
}

RequestHeaderFormatter::RequestHeaderFormatter(const std::string& field_name, bool append)
    : append_(append) {
  if (field_name == "PROTOCOL") {
    field_extractor_ = [](const Envoy::AccessLog::RequestInfo& request_info) {
      return Envoy::AccessLog::AccessLogFormatUtils::protocolToString(request_info.protocol());
    };
  } else if (field_name == "CLIENT_IP") {
    field_extractor_ = [](const Envoy::AccessLog::RequestInfo& request_info) {
      return request_info.getDownstreamAddress();
    };
  } else {
    throw EnvoyException(
        fmt::format("field '{}' not supported as custom request header", field_name));
  }
}

const std::string
RequestHeaderFormatter::format(const Envoy::AccessLog::RequestInfo& request_info) const {
  return field_extractor_(request_info);
}

} // namespace Router
} // namespace Envoy
