#pragma once

#include <functional>
#include <string>
#include <vector>

#include "envoy/http/access_log.h"

namespace Envoy {
namespace Http {
namespace AccessLog {

/**
 * Util class for ResponseFlags.
 */
class ResponseFlagUtils {
public:
  static const std::string toShortString(const RequestInfo& request_info);

private:
  ResponseFlagUtils();
  static void appendString(std::string& result, const std::string& append);

  const static std::string NONE;
  const static std::string FAILED_LOCAL_HEALTH_CHECK;
  const static std::string NO_HEALTHY_UPSTREAM;
  const static std::string UPSTREAM_REQUEST_TIMEOUT;
  const static std::string LOCAL_RESET;
  const static std::string UPSTREAM_REMOTE_RESET;
  const static std::string UPSTREAM_CONNECTION_FAILURE;
  const static std::string UPSTREAM_CONNECTION_TERMINATION;
  const static std::string UPSTREAM_OVERFLOW;
  const static std::string NO_ROUTE_FOUND;
  const static std::string DELAY_INJECTED;
  const static std::string FAULT_INJECTED;
  const static std::string RATE_LIMITED;
};

/**
 * Access log format parser.
 */
class AccessLogFormatParser {
public:
  static std::vector<FormatterPtr> parse(const std::string& format);

private:
  static void parseCommand(const std::string& token, const size_t start, std::string& main_header,
                           std::string& alternative_header, Optional<size_t>& max_length);
};

/**
 * Util class for access log format.
 */
class AccessLogFormatUtils {
public:
  static FormatterPtr defaultAccessLogFormatter();
  static const std::string& protocolToString(Protocol protocol);

private:
  AccessLogFormatUtils();

  static const std::string DEFAULT_FORMAT;
};

/**
 * Composite formatter implementation.
 */
class FormatterImpl : public Formatter {
public:
  FormatterImpl(const std::string& format);

  // Formatter::format
  std::string format(const HeaderMap& request_headers, const HeaderMap& response_headers,
                     const RequestInfo& request_info) const override;

private:
  std::vector<FormatterPtr> formatters_;
};

/**
 * Formatter for string literal. It ignores headers and request info and returns string by which it
 * was initialized.
 */
class PlainStringFormatter : public Formatter {
public:
  PlainStringFormatter(const std::string& str);

  // Formatter::format
  std::string format(const HeaderMap&, const HeaderMap&, const RequestInfo&) const override;

private:
  std::string str_;
};

class HeaderFormatter {
public:
  HeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                  const Optional<size_t>& max_length);

  std::string format(const HeaderMap& headers) const;

private:
  LowerCaseString main_header_;
  LowerCaseString alternative_header_;
  Optional<size_t> max_length_;
};

/**
 * Formatter based on request header.
 */
class RequestHeaderFormatter : public Formatter, HeaderFormatter {
public:
  RequestHeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                         const Optional<size_t>& max_length);

  // Formatter::format
  std::string format(const HeaderMap& request_headers, const HeaderMap&,
                     const RequestInfo&) const override;
};

/**
 * Formatter based on the response header.
 */
class ResponseHeaderFormatter : public Formatter, HeaderFormatter {
public:
  ResponseHeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                          const Optional<size_t>& max_length);

  // Formatter::format
  std::string format(const HeaderMap&, const HeaderMap& response_headers,
                     const RequestInfo&) const override;
};

/**
 * Formatter based on the RequestInfo field.
 */
class RequestInfoFormatter : public Formatter {
public:
  RequestInfoFormatter(const std::string& field_name);

  // Formatter::format
  std::string format(const HeaderMap&, const HeaderMap&,
                     const RequestInfo& request_info) const override;

private:
  std::function<std::string(const RequestInfo&)> field_extractor_;
};

} // namespace AccessLog
} // namespace Http
} // namespace Envoy
