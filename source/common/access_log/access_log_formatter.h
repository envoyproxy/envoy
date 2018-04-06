#pragma once

#include <functional>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/common/time.h"
#include "envoy/request_info/request_info.h"

#include "common/common/utility.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace AccessLog {

/**
 * Access log format parser.
 */
class AccessLogFormatParser {
public:
  static std::vector<FormatterPtr> parse(const std::string& format);

private:
  static void parseCommand(const std::string& token, const size_t start, std::string& main_header,
                           std::string& alternative_header, absl::optional<size_t>& max_length);

  // the indexes of where the parameters for each directive is expected to begin
  static const size_t ReqParamStart{std::strlen("REQ(")};
  static const size_t RespParamStart{std::strlen("RESP(")};
  static const size_t StartTimeParamStart{std::strlen("START_TIME(")};
};

/**
 * Util class for access log format.
 */
class AccessLogFormatUtils {
public:
  static FormatterPtr defaultAccessLogFormatter();
  static const std::string& protocolToString(const absl::optional<Http::Protocol>& protocol);
  static std::string durationToString(const absl::optional<std::chrono::nanoseconds>& time);

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
  std::string format(const Http::HeaderMap& request_headers,
                     const Http::HeaderMap& response_headers,
                     const RequestInfo::RequestInfo& request_info) const override;

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
  std::string format(const Http::HeaderMap&, const Http::HeaderMap&,
                     const RequestInfo::RequestInfo&) const override;

private:
  std::string str_;
};

class HeaderFormatter {
public:
  HeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                  const absl::optional<size_t>& max_length);

  std::string format(const Http::HeaderMap& headers) const;

private:
  Http::LowerCaseString main_header_;
  Http::LowerCaseString alternative_header_;
  absl::optional<size_t> max_length_;
};

/**
 * Formatter based on request header.
 */
class RequestHeaderFormatter : public Formatter, HeaderFormatter {
public:
  RequestHeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                         const absl::optional<size_t>& max_length);

  // Formatter::format
  std::string format(const Http::HeaderMap& request_headers, const Http::HeaderMap&,
                     const RequestInfo::RequestInfo&) const override;
};

/**
 * Formatter based on the response header.
 */
class ResponseHeaderFormatter : public Formatter, HeaderFormatter {
public:
  ResponseHeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                          const absl::optional<size_t>& max_length);

  // Formatter::format
  std::string format(const Http::HeaderMap&, const Http::HeaderMap& response_headers,
                     const RequestInfo::RequestInfo&) const override;
};

/**
 * Formatter based on the RequestInfo field.
 */
class RequestInfoFormatter : public Formatter {
public:
  RequestInfoFormatter(const std::string& field_name);

  // Formatter::format
  std::string format(const Http::HeaderMap&, const Http::HeaderMap&,
                     const RequestInfo::RequestInfo& request_info) const override;

private:
  std::function<std::string(const RequestInfo::RequestInfo&)> field_extractor_;
};

/**
 * Formatter
 */
class StartTimeFormatter : public Formatter {
public:
  StartTimeFormatter(const std::string& format);
  std::string format(const Http::HeaderMap&, const Http::HeaderMap&,
                     const RequestInfo::RequestInfo&) const override;

private:
  const Envoy::DateFormatter date_formatter_;
};

} // namespace AccessLog
} // namespace Envoy
