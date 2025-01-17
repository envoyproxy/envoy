#pragma once

#include <bitset>
#include <functional>
#include <list>
#include <regex>
#include <string>
#include <vector>

#include "envoy/formatter/substitution_formatter.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/utility.h"
#include "source/common/formatter/substitution_format_utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Formatter {

/**
 * FormatterProvider for local_reply_body. It returns the string from `local_reply_body` argument.
 */
class LocalReplyBodyFormatter : public FormatterProvider {
public:
  LocalReplyBodyFormatter() = default;

  // Formatter::format
  absl::optional<std::string>
  formatWithContext(const HttpFormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override;
  ProtobufWkt::Value
  formatValueWithContext(const HttpFormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override;
};

/**
 * FormatterProvider for access log type. It returns the string from `access_log_type` argument.
 */
class AccessLogTypeFormatter : public FormatterProvider {
public:
  AccessLogTypeFormatter() = default;

  // Formatter::format
  absl::optional<std::string>
  formatWithContext(const HttpFormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override;
  ProtobufWkt::Value
  formatValueWithContext(const HttpFormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override;
};

class HeaderFormatter {
public:
  HeaderFormatter(absl::string_view main_header, absl::string_view alternative_header,
                  absl::optional<size_t> max_length);

protected:
  absl::optional<std::string> format(const Http::HeaderMap& headers) const;
  ProtobufWkt::Value formatValue(const Http::HeaderMap& headers) const;

private:
  const Http::HeaderEntry* findHeader(const Http::HeaderMap& headers) const;

  Http::LowerCaseString main_header_;
  Http::LowerCaseString alternative_header_;
  absl::optional<size_t> max_length_;
};

/**
 * FormatterProvider for headers byte size.
 */
class HeadersByteSizeFormatter : public FormatterProvider {
public:
  // TODO(taoxuy): Add RequestTrailers here.
  enum class HeaderType { RequestHeaders, ResponseHeaders, ResponseTrailers };

  HeadersByteSizeFormatter(const HeaderType header_type);

  absl::optional<std::string>
  formatWithContext(const HttpFormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override;
  ProtobufWkt::Value
  formatValueWithContext(const HttpFormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override;

private:
  uint64_t extractHeadersByteSize(const Http::RequestHeaderMap& request_headers,
                                  const Http::ResponseHeaderMap& response_headers,
                                  const Http::ResponseTrailerMap& response_trailers) const;
  HeaderType header_type_;
};

/**
 * FormatterProvider for request headers.
 */
class RequestHeaderFormatter : public FormatterProvider, HeaderFormatter {
public:
  RequestHeaderFormatter(absl::string_view main_header, absl::string_view alternative_header,
                         absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const HttpFormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override;
  ProtobufWkt::Value
  formatValueWithContext(const HttpFormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override;
};

/**
 * FormatterProvider for response headers.
 */
class ResponseHeaderFormatter : public FormatterProvider, HeaderFormatter {
public:
  ResponseHeaderFormatter(absl::string_view main_header, absl::string_view alternative_header,
                          absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const HttpFormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override;
  ProtobufWkt::Value
  formatValueWithContext(const HttpFormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override;
};

/**
 * FormatterProvider for response trailers.
 */
class ResponseTrailerFormatter : public FormatterProvider, HeaderFormatter {
public:
  ResponseTrailerFormatter(absl::string_view main_header, absl::string_view alternative_header,
                           absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const HttpFormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override;
  ProtobufWkt::Value
  formatValueWithContext(const HttpFormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override;
};

/**
 * FormatterProvider for trace ID.
 */
class TraceIDFormatter : public FormatterProvider {
public:
  absl::optional<std::string>
  formatWithContext(const HttpFormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override;
  ProtobufWkt::Value
  formatValueWithContext(const HttpFormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override;
};

class GrpcStatusFormatter : public FormatterProvider, HeaderFormatter {
public:
  enum Format {
    CamelString,
    SnakeString,
    Number,
  };

  GrpcStatusFormatter(const std::string& main_header, const std::string& alternative_header,
                      absl::optional<size_t> max_length, Format format);

  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const HttpFormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override;
  ProtobufWkt::Value
  formatValueWithContext(const HttpFormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override;

  static Format parseFormat(absl::string_view format);

private:
  const Format format_;
};

/**
 * FormatterProvider for request headers from StreamInfo (rather than the request_headers param).
 * Purely for testing.
 */
class StreamInfoRequestHeaderFormatter : public FormatterProvider, HeaderFormatter {
public:
  StreamInfoRequestHeaderFormatter(const std::string& main_header,
                                   const std::string& alternative_header,
                                   absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const HttpFormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override;
  ProtobufWkt::Value
  formatValueWithContext(const HttpFormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override;
};

class BuiltInHttpCommandParser : public CommandParser {
public:
  BuiltInHttpCommandParser() = default;

  // CommandParser
  FormatterProviderPtr parse(absl::string_view command, absl::string_view subcommand,
                             absl::optional<size_t> max_length) const override;

private:
  using FormatterProviderCreateFunc =
      std::function<FormatterProviderPtr(absl::string_view, absl::optional<size_t>)>;

  using FormatterProviderLookupTbl =
      absl::flat_hash_map<absl::string_view, std::pair<CommandSyntaxChecker::CommandSyntaxFlags,
                                                       FormatterProviderCreateFunc>>;
  static const FormatterProviderLookupTbl& getKnownFormatters();
};

using BuiltInHttpCommandParserFactory = BuiltInCommandParserFactoryBase<HttpFormatterContext>;
class DefaultBuiltInHttpCommandParserFactory : public BuiltInHttpCommandParserFactory {
public:
  std::string name() const override;
  CommandParserPtr createCommandParser() const override;
};

DECLARE_FACTORY(DefaultBuiltInHttpCommandParserFactory);

} // namespace Formatter
} // namespace Envoy
