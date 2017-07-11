#pragma once

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>

#include "common/common/logger.h"
#include "common/http/access_log/access_log_formatter.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Router {

/**
 * Interface for all types of header formatters used for custom request headers.
 */
class HeaderFormatter {

public:
  virtual ~HeaderFormatter(){};

  virtual std::string format(const Envoy::Http::AccessLog::RequestInfo& request_info) const PURE;
};

typedef std::shared_ptr<HeaderFormatter> HeaderFormatterSharedPtr;

/**
 * a formatter that expands the request header variable to a value based on info in RequestInfo.
 */
class RequestHeaderFormatter : public HeaderFormatter, Logger::Loggable<Logger::Id::config> {

public:
  virtual ~RequestHeaderFormatter(){};
  RequestHeaderFormatter(std::string& field_name);

  // HeaderFormatter::format
  std::string format(const Envoy::Http::AccessLog::RequestInfo& request_info) const override;

private:
  std::function<std::string(const Envoy::Http::AccessLog::RequestInfo&)> field_extractor_;
};
/**
 * Returns back the same static header value.
 */
class PlainHeaderFormatter : public HeaderFormatter {

public:
  virtual ~PlainHeaderFormatter(){};

  PlainHeaderFormatter(const std::string& static_header_value) {
    static_value_ = static_header_value;
  };

  // HeaderFormatter::format
  std::string format(const Envoy::Http::AccessLog::RequestInfo&) const override {
    return static_value_;
  };

private:
  std::string static_value_;
};

// forward declaring the typedef and class
class RequestHeaderParser;

typedef std::shared_ptr<RequestHeaderParser> RequestHeaderParserSharedPtr;

/**
 * This class will hold the parsing logic required during configuration build and
 * also perform evaluation for the variables at runtime.
 */
class RequestHeaderParser : Logger::Loggable<Logger::Id::config> {

public:
  virtual ~RequestHeaderParser(){};

  static RequestHeaderParserSharedPtr parse(const Json::Object& config);

  static HeaderFormatterSharedPtr parseInternal(const std::string& format);

  void evaluateRequestHeaders(
      Http::HeaderMap& headers, const Http::AccessLog::RequestInfo& requestInfo,
      const std::list<std::pair<Http::LowerCaseString, std::string>>& requestHeadersToAdd) const;

  std::unordered_map<Http::LowerCaseString, HeaderFormatterSharedPtr, Http::LowerCaseStringHasher>
  headerFormatterMap() {
    return header_formatter_map_;
  };

private:
  /**
   * building a map of request header formatters.
   */
  std::unordered_map<Http::LowerCaseString, HeaderFormatterSharedPtr, Http::LowerCaseStringHasher>
      header_formatter_map_;
};

} // namespace Router
} // namespace Envoy
