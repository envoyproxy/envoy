#include "common/router/req_header_formatter.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/config/rds_json.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Router {

HeaderFormatterPtr RequestHeaderParser::parseInternal(const std::string& format) {
  if (format.find("%") == 0) {
    const size_t last_occ_pos = format.rfind("%");
    if (last_occ_pos == std::string::npos || last_occ_pos <= 1) {
      throw EnvoyException(fmt::format("Incorrect header configuration: {}. Expected the variable "
                                       "to be of format %<variable_name>%",
                                       format));
    }
    const std::string variable_name = format.substr(1, last_occ_pos - 1);
    return HeaderFormatterPtr{new RequestHeaderFormatter(variable_name)};
  } else {
    return HeaderFormatterPtr{new PlainHeaderFormatter(format)};
  }
}

RequestHeaderParserPtr RequestHeaderParser::parse(
    const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers) {
  RequestHeaderParserPtr request_header_parser(new RequestHeaderParser());
  for (const auto& header_value_option : headers) {
    HeaderFormatterPtr header_formatter =
        RequestHeaderParser::parseInternal(header_value_option.header().value());
    request_header_parser->header_formatters_.push_back(
        {Http::LowerCaseString(header_value_option.header().key()), std::move(header_formatter)});
  }

  return request_header_parser;
}

void RequestHeaderParser::evaluateRequestHeaders(
    Http::HeaderMap& headers, const Http::AccessLog::RequestInfo& request_info) const {
  for (const auto& formatter : header_formatters_) {
    const std::string formatted_header_value = formatter.second->format(request_info);
    headers.addReferenceKey(formatter.first, formatted_header_value);
  }
}

RequestHeaderFormatter::RequestHeaderFormatter(const std::string& field_name) {
  if (field_name == "PROTOCOL") {
    field_extractor_ = [](const Envoy::Http::AccessLog::RequestInfo& request_info) {
      return Envoy::Http::AccessLog::AccessLogFormatUtils::protocolToString(
          request_info.protocol());
    };
  } else if (field_name == "CLIENT_IP") {
    field_extractor_ = [](const Envoy::Http::AccessLog::RequestInfo& request_info) {
      return request_info.getDownstreamAddress();
    };
  } else {
    throw EnvoyException(
        fmt::format("field '{}' not supported as custom request header", field_name));
  }
}

const std::string
RequestHeaderFormatter::format(const Envoy::Http::AccessLog::RequestInfo& request_info) const {
  return field_extractor_(request_info);
}
} // namespace Router
} // namespace Envoy
