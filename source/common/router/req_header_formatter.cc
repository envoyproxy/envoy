#include "common/router/req_header_formatter.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/http/access_log/access_log_formatter.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Router {

HeaderFormatterSharedPtr RequestHeaderParser::parseInternal(const std::string& format) {
  std::string variable_name;
  if (format.find("%") == 0) {
    size_t last_occ_pos = format.rfind("%");
    if (last_occ_pos == std::string::npos || last_occ_pos <= 1) {
      throw EnvoyException(fmt::format(
          "Incorrect configuration: {}. Expected the variable to be of format %<variable_name>%",
          format));
    }
    variable_name = format.substr(1, last_occ_pos - 1);

  } else {
    HeaderFormatterSharedPtr plain_header_formatter_shared_ptr(new PlainHeaderFormatter(format));
    return plain_header_formatter_shared_ptr;
  }
  HeaderFormatterSharedPtr request_header_formatter_shared_ptr(
      new RequestHeaderFormatter(variable_name));
  return request_header_formatter_shared_ptr;
}

RequestHeaderParserSharedPtr RequestHeaderParser::parse(const Json::Object& config) {

  RequestHeaderParserSharedPtr request_header_parser(new RequestHeaderParser());
  if (config.hasObject("request_headers_to_add")) {
    std::vector<Json::ObjectSharedPtr> request_headers =
        config.getObjectArray("request_headers_to_add");
    for (const Json::ObjectSharedPtr& header : request_headers) {
      if (!header->getString("key").empty() && !header->getString("value").empty()) {
        ENVOY_LOG(debug, "adding key {} to header formatter map", header->getString("key"));
        request_header_parser->header_formatter_map_.emplace(
            Http::LowerCaseString(header->getString("key")),
            RequestHeaderParser::parseInternal(header->getString("value")));
      }
    }
  }

  return request_header_parser;
}

void RequestHeaderParser::evaluateRequestHeaders(
    Http::HeaderMap& headers, const Http::AccessLog::RequestInfo& requestInfo,
    const std::list<std::pair<Http::LowerCaseString, std::string>>& requestHeadersToAdd) const {

  for (const auto& to_add : requestHeadersToAdd) {
    ENVOY_LOG(debug, "request headers key {}", to_add.first.get());
    auto search = header_formatter_map_.find(to_add.first);
    if (search != header_formatter_map_.end()) {
      const std::string formatted_header_value = search->second->format(requestInfo);
      headers.addStaticKey(to_add.first, formatted_header_value);
    } else {
      headers.addStatic(to_add.first, to_add.second);
    }
  }
}

RequestHeaderFormatter::RequestHeaderFormatter(std::string& field_name) {
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

std::string
RequestHeaderFormatter::format(const Envoy::Http::AccessLog::RequestInfo& request_info) const {
  return field_extractor_(request_info);
}
} // namespace Router
} // namespace Envoy
