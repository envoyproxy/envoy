#include "common/router/req_header_formatter.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/config/rds_json.h"
#include "common/http/access_log/access_log_formatter.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Router {

HeaderFormatterPtr RequestHeaderParser::parseInternal(const std::string& format) {
  if (format.find("%") == 0) {
    size_t last_occ_pos = format.rfind("%");
    if (last_occ_pos == std::string::npos || last_occ_pos <= 1) {
      throw EnvoyException(fmt::format(
          "Incorrect configuration: {}. Expected the variable to be of format %<variable_name>%",
          format));
    }
    const std::string variable_name = format.substr(1, last_occ_pos - 1);
    HeaderFormatterPtr request_header_formatter_ptr(new RequestHeaderFormatter(variable_name));
    return request_header_formatter_ptr;
  } else {
    HeaderFormatterPtr plain_header_formatter_ptr(new PlainHeaderFormatter(format));
    return plain_header_formatter_ptr;
  }
}

RequestHeaderParserPtr RequestHeaderParser::parseRoute(const envoy::api::v2::Route& route) {
  return parse(route.route().request_headers_to_add());
}

RequestHeaderParserPtr
RequestHeaderParser::parseVirtualHost(const envoy::api::v2::VirtualHost& virtualHost) {
  return parse(virtualHost.request_headers_to_add());
}

RequestHeaderParserPtr RequestHeaderParser::parseRouteConfiguration(
    const envoy::api::v2::RouteConfiguration& routeConfig) {
  return parse(routeConfig.request_headers_to_add());
}

RequestHeaderParserPtr RequestHeaderParser::parse(
    const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers) {
  RequestHeaderParserPtr request_header_parser(new RequestHeaderParser());
  for (const auto& header_value_option : headers) {
    ENVOY_LOG(debug, "adding key {} to header formatter map", header_value_option.header().key());
    HeaderFormatterPtr header_formatter =
        RequestHeaderParser::parseInternal(header_value_option.header().value());
    request_header_parser->header_formatter_map_.emplace(
        Http::LowerCaseString(header_value_option.header().key()), std::move(header_formatter));
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
      headers.addReferenceKey(to_add.first, formatted_header_value);
    } else {
      headers.addReference(to_add.first, to_add.second);
    }
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
