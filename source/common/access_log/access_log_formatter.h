#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/common/time.h"
#include "envoy/stream_info/stream_info.h"

#include "common/common/utility.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace AccessLog {

/**
 * Access log format parser.
 */
class AccessLogFormatParser {
public:
  static std::vector<FormatterProviderPtr> parse(const std::string& format);

private:
  /**
   * Parse a header format rule of the form: %REQ(X?Y):Z% .
   * Will populate a main_header and an optional alternative header if specified.
   * See doc:
   * docs/root/configuration/access_log.rst#format-rules
   */
  static void parseCommandHeader(const std::string& token, const size_t start,
                                 std::string& main_header, std::string& alternative_header,
                                 absl::optional<size_t>& max_length);

  /**
   * General parse command utility. Will parse token from start position. Token is expected to end
   * with ')'. An optional ":max_length" may be specified after the closing ')' char. Token may
   * contain multiple values separated by "separator" string. First value will be populated in
   * "main" and any additional sub values will be set in the vector "subitems". For example token
   * of: "com.test.my_filter:test_object:inner_key):100" with separator of ":" will set the
   * following:
   * - main: com.test.my_filter
   * - subitems: {test_object, inner_key}
   * - max_length: 100
   *
   * @param token the token to parse
   * @param start the index to start parsing from
   * @param separator separator between values
   * @param main the first value
   * @param sub_items any additional values
   * @param max_length optional max_length will be populated if specified
   *
   * TODO(glicht) Rewrite with a parser library. See:
   * https://github.com/envoyproxy/envoy/issues/2967
   */
  static void parseCommand(const std::string& token, const size_t start,
                           const std::string& separator, std::string& main,
                           std::vector<std::string>& sub_items, absl::optional<size_t>& max_length);

  // the indexes of where the parameters for each directive is expected to begin
  static const size_t ReqParamStart{sizeof("REQ(") - 1};
  static const size_t RespParamStart{sizeof("RESP(") - 1};
  static const size_t TrailParamStart{sizeof("TRAILER(") - 1};
  static const size_t StartTimeParamStart{sizeof("START_TIME(") - 1};
};

/**
 * Util class for access log format.
 */
class AccessLogFormatUtils {
public:
  static FormatterPtr defaultAccessLogFormatter();
  static const std::string& protocolToString(const absl::optional<Http::Protocol>& protocol);
  static std::string durationToString(const absl::optional<std::chrono::nanoseconds>& time);
  static std::string durationToString(const std::chrono::nanoseconds& time);

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
                     const Http::HeaderMap& response_trailers,
                     const StreamInfo::StreamInfo& stream_info,
                     const absl::string_view& response_body) const override;

private:
  std::vector<FormatterProviderPtr> providers_;
};

class JsonFormatterImpl : public Formatter {
public:
  JsonFormatterImpl(std::unordered_map<std::string, std::string>& format_mapping);

  // Formatter::format
  std::string format(const Http::HeaderMap& request_headers,
                     const Http::HeaderMap& response_headers,
                     const Http::HeaderMap& response_trailers,
                     const StreamInfo::StreamInfo& stream_info,
                     const absl::string_view& response_body) const override;

private:
  std::vector<FormatterProviderPtr> providers_;
  std::map<const std::string, Envoy::AccessLog::FormatterPtr> json_output_format_;

  std::unordered_map<std::string, std::string> toMap(const Http::HeaderMap& request_headers,
                                                     const Http::HeaderMap& response_headers,
                                                     const Http::HeaderMap& response_trailers,
                                                     const StreamInfo::StreamInfo& stream_info,
                                                     const absl::string_view& body) const;
};

/**
 * Formatter for string literal. It ignores headers and stream info and returns string by which it
 * was initialized.
 */
class PlainStringFormatter : public FormatterProvider {
public:
  PlainStringFormatter(const std::string& str);

  // Formatter::format
  std::string format(const Http::HeaderMap&, const Http::HeaderMap&, const Http::HeaderMap&,
                     const StreamInfo::StreamInfo&, const absl::string_view&) const override;

private:
  std::string str_;
};

/**
 * Formatter for string literal. It ignores headers and stream info and returns string by which it
 * was initialized.
 */
class BodyFormatter : public FormatterProvider {
public:
  BodyFormatter();

  // Formatter::format
  std::string format(const Http::HeaderMap&, const Http::HeaderMap&, const Http::HeaderMap&,
                     const StreamInfo::StreamInfo&,
                     const absl::string_view& response_body) const override;
};

class HeaderFormatter {
public:
  HeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                  absl::optional<size_t> max_length);

  std::string format(const Http::HeaderMap& headers) const;

private:
  Http::LowerCaseString main_header_;
  Http::LowerCaseString alternative_header_;
  absl::optional<size_t> max_length_;
};

/**
 * Formatter based on request header.
 */
class RequestHeaderFormatter : public FormatterProvider, HeaderFormatter {
public:
  RequestHeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                         absl::optional<size_t> max_length);

  // Formatter::format
  std::string format(const Http::HeaderMap& request_headers, const Http::HeaderMap&,
                     const Http::HeaderMap&, const StreamInfo::StreamInfo&,
                     const absl::string_view&) const override;
};

/**
 * Formatter based on the response header.
 */
class ResponseHeaderFormatter : public FormatterProvider, HeaderFormatter {
public:
  ResponseHeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                          absl::optional<size_t> max_length);

  // Formatter::format
  std::string format(const Http::HeaderMap&, const Http::HeaderMap& response_headers,
                     const Http::HeaderMap&, const StreamInfo::StreamInfo&,
                     const absl::string_view&) const override;
};

/**
 * Formatter based on the response trailer.
 */
class ResponseTrailerFormatter : public FormatterProvider, HeaderFormatter {
public:
  ResponseTrailerFormatter(const std::string& main_header, const std::string& alternative_header,
                           absl::optional<size_t> max_length);

  // Formatter::format
  std::string format(const Http::HeaderMap&, const Http::HeaderMap&,
                     const Http::HeaderMap& response_trailers, const StreamInfo::StreamInfo&,
                     const absl::string_view&) const override;
};

/**
 * Formatter based on the StreamInfo field.
 */
class StreamInfoFormatter : public FormatterProvider {
public:
  StreamInfoFormatter(const std::string& field_name);

  // Formatter::format
  std::string format(const Http::HeaderMap&, const Http::HeaderMap&, const Http::HeaderMap&,
                     const StreamInfo::StreamInfo& stream_info,
                     const absl::string_view&) const override;

  using FieldExtractor = std::function<std::string(const StreamInfo::StreamInfo&)>;

private:
  FieldExtractor field_extractor_;
};

/**
 * Base formatter for formatting Metadata objects
 */
class MetadataFormatter {
public:
  MetadataFormatter(const std::string& filter_namespace, const std::vector<std::string>& path,
                    absl::optional<size_t> max_length);

  std::string format(const envoy::api::v2::core::Metadata& metadata) const;

private:
  std::string filter_namespace_;
  std::vector<std::string> path_;
  absl::optional<size_t> max_length_;
};

/**
 * Formatter based on the DynamicMetadata from StreamInfo.
 */
class DynamicMetadataFormatter : public FormatterProvider, MetadataFormatter {
public:
  DynamicMetadataFormatter(const std::string& filter_namespace,
                           const std::vector<std::string>& path, absl::optional<size_t> max_length);

  // FormatterProvider::format
  std::string format(const Http::HeaderMap&, const Http::HeaderMap&, const Http::HeaderMap&,
                     const StreamInfo::StreamInfo& stream_info,
                     const absl::string_view&) const override;
};

/**
 * Formatter
 */
class StartTimeFormatter : public FormatterProvider {
public:
  StartTimeFormatter(const std::string& format);
  std::string format(const Http::HeaderMap&, const Http::HeaderMap&, const Http::HeaderMap&,
                     const StreamInfo::StreamInfo&, const absl::string_view&) const override;

private:
  const Envoy::DateFormatter date_formatter_;
};

} // namespace AccessLog
} // namespace Envoy
