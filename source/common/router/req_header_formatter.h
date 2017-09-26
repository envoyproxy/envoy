#pragma once

#include <cstdint>
#include <string>

#include "common/http/access_log/access_log_formatter.h"
#include "common/protobuf/protobuf.h"

#include "api/rds.pb.h"

namespace Envoy {
namespace Router {

/**
 * Interface for all types of header formatters used for custom request headers.
 */
class HeaderFormatter {
public:
  virtual ~HeaderFormatter() {}

  virtual const std::string
  format(const Envoy::Http::AccessLog::RequestInfo& request_info) const PURE;
};

typedef std::unique_ptr<HeaderFormatter> HeaderFormatterPtr;

/**
 * A formatter that expands the request header variable to a value based on info in RequestInfo.
 */
class RequestHeaderFormatter : public HeaderFormatter {
public:
  RequestHeaderFormatter(const std::string& field_name);

  // HeaderFormatter::format
  const std::string format(const Envoy::Http::AccessLog::RequestInfo& request_info) const override;

private:
  std::function<std::string(const Envoy::Http::AccessLog::RequestInfo&)> field_extractor_;
};

/**
 * A formatter that returns back the same static header value.
 */
class PlainHeaderFormatter : public HeaderFormatter {
public:
  PlainHeaderFormatter(const std::string& static_header_value)
      : static_value_(static_header_value){};

  // HeaderFormatter::format
  const std::string format(const Envoy::Http::AccessLog::RequestInfo&) const override {
    return static_value_;
  };

private:
  const std::string static_value_;
};

class RequestHeaderParser;
typedef std::unique_ptr<RequestHeaderParser> RequestHeaderParserPtr;

/**
 * This class holds the parsing logic required during configuration build and
 * also perform evaluation for the variables at runtime.
 */
class RequestHeaderParser {
public:
  virtual ~RequestHeaderParser() {}

  static RequestHeaderParserPtr
  parse(const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers);

  void evaluateRequestHeaders(Http::HeaderMap& headers,
                              const Http::AccessLog::RequestInfo& request_info) const;

private:
  std::list<std::pair<Http::LowerCaseString, HeaderFormatterPtr>> header_formatters_;

  static HeaderFormatterPtr parseInternal(const std::string& format);
};

} // namespace Router
} // namespace Envoy
