#pragma once

#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/http/header_map.h"

#include "common/protobuf/protobuf.h"
#include "common/router/header_formatter.h"

#include "api/base.pb.h"

namespace Envoy {
namespace Router {

class RequestHeaderParser;
typedef std::unique_ptr<RequestHeaderParser> RequestHeaderParserPtr;

/**
 * This class provides request-time generation of upstream request headers. Header configurations
 * are pre-parsed to select between constant values and values based on the evaluation of
 * AccessLog::RequestInfo fields.
 */
class RequestHeaderParser {
public:
  static RequestHeaderParserPtr
  parse(const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers);

  void evaluateRequestHeaders(Http::HeaderMap& headers,
                              const AccessLog::RequestInfo& request_info) const;

private:
  std::vector<std::pair<Http::LowerCaseString, HeaderFormatterPtr>> header_formatters_;
};

class ResponseHeaderParser;
typedef std::unique_ptr<ResponseHeaderParser> ResponseHeaderParserPtr;

/**
 * This class provides request-time generation of response headers. Header configurations are
 * assumed to reference constant values.
 */
class ResponseHeaderParser {
public:
  static ResponseHeaderParserPtr
  parse(const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers_to_add,
        const Protobuf::RepeatedPtrField<ProtobufTypes::String>& headers_to_remove);

  void evaluateResponseHeaders(Http::HeaderMap& headers,
                               const AccessLog::RequestInfo& request_info) const;

private:
  std::vector<std::pair<Http::LowerCaseString, HeaderFormatterPtr>> headers_to_add_;
  std::vector<Http::LowerCaseString> headers_to_remove_;
};

} // namespace Router
} // namespace Envoy
