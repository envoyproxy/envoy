#pragma once

#include <bitset>
#include <functional>
#include <list>
#include <string>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Formatter {

class CommandSyntaxChecker {
public:
  using CommandSyntaxFlags = std::bitset<4>;
  static constexpr CommandSyntaxFlags COMMAND_ONLY = 0;
  static constexpr CommandSyntaxFlags PARAMS_REQUIRED = 1 << 0;
  static constexpr CommandSyntaxFlags PARAMS_OPTIONAL = 1 << 1;
  static constexpr CommandSyntaxFlags LENGTH_ALLOWED = 1 << 2;

  static void verifySyntax(CommandSyntaxChecker::CommandSyntaxFlags flags,
                           const std::string& command, const std::string& subcommand,
                           const absl::optional<size_t>& length);
};

/**
 * Access log format parser.
 */
class SubstitutionFormatParser {
public:
  static std::vector<FormatterProviderPtr> parse(const std::string& format);
  static std::vector<FormatterProviderPtr>
  parse(const std::string& format, const std::vector<CommandParserPtr>& command_parsers);

  /**
   * Parse a header subcommand of the form: X?Y .
   * Will populate a main_header and an optional alternative header if specified.
   * See doc:
   * https://envoyproxy.io/docs/envoy/latest/configuration/observability/access_log/access_log#format-rules
   */
  static void parseSubcommandHeaders(const std::string& subcommand, std::string& main_header,
                                     std::string& alternative_header);

  /* Variadic function template to parse the
     subcommand and assign found tokens to sequence of params.
     subcommand must be a sequence
     of tokens separated by the same separator, like: header1:header2 or header1?header2?header3.
     params must be a sequence of std::string& with optional container storing std::string. Here are
     examples of params:
     - std::string& token1
     - std::string& token1, std::string& token2
     - std::string& token1, std::string& token2, std::vector<std::string>& remaining

     If command contains more tokens than number of passed params, unassigned tokens will be
     ignored. If command contains less tokens than number of passed params, some params will be left
     untouched.
  */
  template <typename... Tokens>
  static void parseSubcommand(const std::string& subcommand, const char separator,
                              Tokens&&... params) {
    std::vector<absl::string_view> tokens;
    tokens = absl::StrSplit(subcommand, separator);
    std::vector<absl::string_view>::iterator it = tokens.begin();
    (
        [&](auto& param) {
          if (it != tokens.end()) {
            if constexpr (std::is_same_v<typename std::remove_reference<decltype(param)>::type,
                                         std::string>) {
              // Compile time handler for std::string.
              param = std::string(*it);
              it++;
            } else {
              // Compile time handler for container type. It will catch all remaining tokens and
              // move iterator to the end.
              do {
                param.push_back(std::string(*it));
                it++;
              } while (it != tokens.end());
            }
          }
        }(params),
        ...);
  }
};

/**
 * Util class for access log format.
 */
class SubstitutionFormatUtils {
public:
  static FormatterPtr defaultSubstitutionFormatter();
  // Optional references are not supported, but this method has large performance
  // impact, so using reference_wrapper.
  static const absl::optional<std::reference_wrapper<const std::string>>
  protocolToString(const absl::optional<Http::Protocol>& protocol);
  static const std::string&
  protocolToStringOrDefault(const absl::optional<Http::Protocol>& protocol);
  static const absl::optional<std::string> getHostname();
  static const std::string getHostnameOrDefault();

  /**
   * Unspecified value for protobuf.
   */
  static const ProtobufWkt::Value& unspecifiedValue();

  /**
   * Truncate a string to a maximum length. Do nothing if max_length is not set or
   * max_length is greater than the length of the string.
   */
  static void truncate(std::string& str, absl::optional<size_t> max_length);

private:
  static const std::string DEFAULT_FORMAT;
};

/**
 * Composite formatter implementation.
 */
class FormatterImpl : public Formatter {
public:
  FormatterImpl(const std::string& format, bool omit_empty_values = false);
  FormatterImpl(const std::string& format, bool omit_empty_values,
                const std::vector<CommandParserPtr>& command_parsers);

  // Formatter::format
  std::string format(const Http::RequestHeaderMap& request_headers,
                     const Http::ResponseHeaderMap& response_headers,
                     const Http::ResponseTrailerMap& response_trailers,
                     const StreamInfo::StreamInfo& stream_info, absl::string_view local_reply_body,
                     AccessLog::AccessLogType access_log_type) const override;

private:
  const std::string& empty_value_string_;
  std::vector<FormatterProviderPtr> providers_;
};

// Helper classes for StructFormatter::StructFormatMapVisitor.
template <class... Ts> struct StructFormatMapVisitorHelper : Ts... { using Ts::operator()...; };
template <class... Ts> StructFormatMapVisitorHelper(Ts...) -> StructFormatMapVisitorHelper<Ts...>;

/**
 * An formatter for structured log formats, which returns a Struct proto that
 * can be converted easily into multiple formats.
 */
class StructFormatter {
public:
  StructFormatter(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                  bool omit_empty_values);
  StructFormatter(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                  bool omit_empty_values, const std::vector<CommandParserPtr>& commands);

  ProtobufWkt::Struct format(const Http::RequestHeaderMap& request_headers,
                             const Http::ResponseHeaderMap& response_headers,
                             const Http::ResponseTrailerMap& response_trailers,
                             const StreamInfo::StreamInfo& stream_info,
                             absl::string_view local_reply_body,
                             AccessLog::AccessLogType access_log_type) const;

private:
  struct StructFormatMapWrapper;
  struct StructFormatListWrapper;
  using StructFormatValue =
      absl::variant<const std::vector<FormatterProviderPtr>, const StructFormatMapWrapper,
                    const StructFormatListWrapper>;
  // Although not required for Struct/JSON, it is nice to have the order of
  // properties preserved between the format and the log entry, thus std::map.
  using StructFormatMap = std::map<std::string, StructFormatValue>;
  using StructFormatMapPtr = std::unique_ptr<StructFormatMap>;
  struct StructFormatMapWrapper {
    StructFormatMapPtr value_;
  };

  using StructFormatList = std::list<StructFormatValue>;
  using StructFormatListPtr = std::unique_ptr<StructFormatList>;
  struct StructFormatListWrapper {
    StructFormatListPtr value_;
  };

  using StructFormatMapVisitor = StructFormatMapVisitorHelper<
      const std::function<ProtobufWkt::Value(const std::vector<FormatterProviderPtr>&)>,
      const std::function<ProtobufWkt::Value(const StructFormatter::StructFormatMapWrapper&)>,
      const std::function<ProtobufWkt::Value(const StructFormatter::StructFormatListWrapper&)>>;

  // Methods for building the format map.
  class FormatBuilder {
  public:
    explicit FormatBuilder(const std::vector<CommandParserPtr>& commands) : commands_(commands) {}
    explicit FormatBuilder() : commands_(absl::nullopt) {}
    std::vector<FormatterProviderPtr> toFormatStringValue(const std::string& string_format) const;
    std::vector<FormatterProviderPtr> toFormatNumberValue(double value) const;
    StructFormatMapWrapper toFormatMapValue(const ProtobufWkt::Struct& struct_format) const;
    StructFormatListWrapper
    toFormatListValue(const ProtobufWkt::ListValue& list_value_format) const;

  private:
    using CommandsRef = std::reference_wrapper<const std::vector<CommandParserPtr>>;
    const absl::optional<CommandsRef> commands_;
  };

  // Methods for doing the actual formatting.
  ProtobufWkt::Value providersCallback(const std::vector<FormatterProviderPtr>& providers,
                                       const Http::RequestHeaderMap& request_headers,
                                       const Http::ResponseHeaderMap& response_headers,
                                       const Http::ResponseTrailerMap& response_trailers,
                                       const StreamInfo::StreamInfo& stream_info,
                                       absl::string_view local_reply_body,
                                       AccessLog::AccessLogType access_log_type) const;
  ProtobufWkt::Value
  structFormatMapCallback(const StructFormatter::StructFormatMapWrapper& format_map,
                          const StructFormatMapVisitor& visitor) const;
  ProtobufWkt::Value
  structFormatListCallback(const StructFormatter::StructFormatListWrapper& format_list,
                           const StructFormatMapVisitor& visitor) const;

  const bool omit_empty_values_;
  const bool preserve_types_;
  const std::string empty_value_;

  const StructFormatMapWrapper struct_output_format_;
};

using StructFormatterPtr = std::unique_ptr<StructFormatter>;

class JsonFormatterImpl : public Formatter {
public:
  JsonFormatterImpl(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                    bool omit_empty_values)
      : struct_formatter_(format_mapping, preserve_types, omit_empty_values) {}
  JsonFormatterImpl(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                    bool omit_empty_values, const std::vector<CommandParserPtr>& commands)
      : struct_formatter_(format_mapping, preserve_types, omit_empty_values, commands) {}

  // Formatter::format
  std::string format(const Http::RequestHeaderMap& request_headers,
                     const Http::ResponseHeaderMap& response_headers,
                     const Http::ResponseTrailerMap& response_trailers,
                     const StreamInfo::StreamInfo& stream_info, absl::string_view local_reply_body,
                     AccessLog::AccessLogType access_log_type) const override;

private:
  const StructFormatter struct_formatter_;
};

} // namespace Formatter
} // namespace Envoy
