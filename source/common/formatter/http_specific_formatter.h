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
  absl::optional<std::string> format(const Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const override;
  Protobuf::Value formatValue(const Context& context,
                              const StreamInfo::StreamInfo& stream_info) const override;
};

/**
 * FormatterProvider for access log type. It returns the string from `access_log_type` argument.
 */
class AccessLogTypeFormatter : public FormatterProvider {
public:
  AccessLogTypeFormatter() = default;

  // Formatter::format
  absl::optional<std::string> format(const Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const override;
  Protobuf::Value formatValue(const Context& context,
                              const StreamInfo::StreamInfo& stream_info) const override;
};

class HeaderFormatter {
public:
  HeaderFormatter(absl::string_view main_header, absl::string_view alternative_header,
                  absl::optional<size_t> max_length);

protected:
  absl::optional<std::string> format(OptRef<const Http::HeaderMap> headers) const;
  Protobuf::Value formatValue(OptRef<const Http::HeaderMap> headers) const;

private:
  const Http::HeaderEntry* findHeader(OptRef<const Http::HeaderMap> headers) const;

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

  absl::optional<std::string> format(const Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const override;
  Protobuf::Value formatValue(const Context& context,
                              const StreamInfo::StreamInfo& stream_info) const override;

private:
  uint64_t extractHeadersByteSize(OptRef<const Http::RequestHeaderMap> request_headers,
                                  OptRef<const Http::ResponseHeaderMap> response_headers,
                                  OptRef<const Http::ResponseTrailerMap> response_trailers) const;
  const HeaderType header_type_{};
};

/**
 * FormatterProvider for request headers.
 */
class RequestHeaderFormatter : public FormatterProvider, HeaderFormatter {
public:
  RequestHeaderFormatter(absl::string_view main_header, absl::string_view alternative_header,
                         absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string> format(const Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const override;
  Protobuf::Value formatValue(const Context& context,
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
  absl::optional<std::string> format(const Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const override;
  Protobuf::Value formatValue(const Context& context,
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
  absl::optional<std::string> format(const Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const override;
  Protobuf::Value formatValue(const Context& context,
                              const StreamInfo::StreamInfo& stream_info) const override;
};

/**
 * FormatterProvider for trace ID.
 */
class TraceIDFormatter : public FormatterProvider {
public:
  absl::optional<std::string> format(const Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const override;
  Protobuf::Value formatValue(const Context& context,
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
  absl::optional<std::string> format(const Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const override;
  Protobuf::Value formatValue(const Context& context,
                              const StreamInfo::StreamInfo& stream_info) const override;

  static Format parseFormat(absl::string_view format);

private:
  const Format format_;
};

class QueryParameterFormatter : public FormatterProvider {
public:
  QueryParameterFormatter(absl::string_view parameter_key, absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string> format(const Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const override;
  Protobuf::Value formatValue(const Context& context,
                              const StreamInfo::StreamInfo& stream_info) const override;

private:
  const std::string parameter_key_;
  absl::optional<size_t> max_length_;
};

class QueryParametersFormatter : public FormatterProvider {
public:
  enum DecodeOption {
    Original,
    Decoded,
  };

  static DecodeOption parseDecodeOption(absl::string_view decoding);

  // FormatterProvider
  absl::optional<std::string> format(const Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const override;
  Protobuf::Value formatValue(const Context& context,
                              const StreamInfo::StreamInfo& stream_info) const override;

  QueryParametersFormatter(DecodeOption option, absl::optional<size_t> max_length)
      : option_(option), max_length_(max_length) {}

private:
  const DecodeOption option_;
  absl::optional<size_t> max_length_;
};

class PathFormatter : public FormatterProvider {
public:
  enum PathFormatterOption {
    OriginalPathOrPath,
    PathOnly,
    OriginalPathOnly,
  };

  static absl::StatusOr<FormatterProviderPtr>
  create(absl::string_view with_query, absl::string_view option, absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string> format(const Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const override;
  Protobuf::Value formatValue(const Context& context,
                              const StreamInfo::StreamInfo& stream_info) const override;

  PathFormatter(bool with_query, PathFormatterOption option, absl::optional<size_t> max_length)
      : with_query_(with_query), option_(option), max_length_(max_length) {}

private:
  const bool with_query_{};
  const PathFormatterOption option_{};
  absl::optional<size_t> max_length_;
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

class DefaultBuiltInHttpCommandParserFactory : public BuiltInCommandParserFactory {
public:
  std::string name() const override;
  CommandParserPtr createCommandParser() const override;
};

DECLARE_FACTORY(DefaultBuiltInHttpCommandParserFactory);

} // namespace Formatter
} // namespace Envoy
