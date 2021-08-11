#pragma once

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

/**
 * Access log format parser.
 */
class SubstitutionFormatParser {
public:
  static std::vector<FormatterProviderPtr> parse(const std::string& format);
  static std::vector<FormatterProviderPtr>
  parse(const std::string& format, const std::vector<CommandParserPtr>& command_parsers);

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
   * General tokenize utility. Will parse command from start position. Command is expected to end
   * with ')'. An optional ":max_length" may be specified after the closing ')' char. Command may
   * contain multiple values separated by "separator" character. Those values will be places
   * into tokens container. If no separator is found, entire command (up to ')') will be
   * placed as only item in the container.
   *
   * @param command the command to parse
   * @param start the index to start parsing from
   * @param separator separator between values
   * @param tokens values found in command separated by separator
   * @param max_length optional max_length will be populated if specified
   *
   * TODO(glicht) Rewrite with a parser library. See:
   * https://github.com/envoyproxy/envoy/issues/2967
   */
  static void tokenizeCommand(const std::string& command, const size_t start, const char separator,
                              std::vector<absl::string_view>& tokens,
                              absl::optional<size_t>& max_length);

  /* Variadic function template which invokes tokenizeCommand method to parse the
     token command and assigns found tokens to sequence of params.
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
  static void parseCommand(const std::string& command, const size_t start, const char separator,
                           absl::optional<size_t>& max_length, Tokens&&... params) {
    std::vector<absl::string_view> tokens;
    tokenizeCommand(command, start, separator, tokens, max_length);
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

  /**
   * Return a FormatterProviderPtr if a built-in command is parsed from the token. This method
   * handles mapping the command name to an appropriate formatter after parsing.
   *
   * TODO(rgs1): this can be refactored into a dispatch table using the command name as the key and
   * the parsing parameters as the value.
   *
   * @param token the token to parse
   * @return FormattterProviderPtr substitution provider for the parsed command or nullptr
   */
  static FormatterProviderPtr parseBuiltinCommand(const std::string& token);

private:
  // the indexes of where the parameters for each directive is expected to begin
  static const size_t ReqParamStart{sizeof("REQ(") - 1};
  static const size_t RespParamStart{sizeof("RESP(") - 1};
  static const size_t TrailParamStart{sizeof("TRAILER(") - 1};
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

private:
  SubstitutionFormatUtils();

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
                     const StreamInfo::StreamInfo& stream_info,
                     absl::string_view local_reply_body) const override;

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
                             absl::string_view local_reply_body) const;

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
                                       absl::string_view local_reply_body) const;
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
                     const StreamInfo::StreamInfo& stream_info,
                     absl::string_view local_reply_body) const override;

private:
  const StructFormatter struct_formatter_;
};

/**
 * FormatterProvider for string literals. It ignores headers and stream info and returns string by
 * which it was initialized.
 */
class PlainStringFormatter : public FormatterProvider {
public:
  PlainStringFormatter(const std::string& str);

  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view) const override;

private:
  ProtobufWkt::Value str_;
};

/**
 * FormatterProvider for local_reply_body. It returns the string from `local_reply_body` argument.
 */
class LocalReplyBodyFormatter : public FormatterProvider {
public:
  LocalReplyBodyFormatter() = default;

  // Formatter::format
  absl::optional<std::string> format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view local_reply_body) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view local_reply_body) const override;
};

class HeaderFormatter {
public:
  HeaderFormatter(const std::string& main_header, const std::string& alternative_header,
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

  absl::optional<std::string> format(const Http::RequestHeaderMap& request_headers,
                                     const Http::ResponseHeaderMap& response_headers,
                                     const Http::ResponseTrailerMap& response_trailers,
                                     const StreamInfo::StreamInfo&,
                                     absl::string_view) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap& request_headers,
                                 const Http::ResponseHeaderMap& response_headers,
                                 const Http::ResponseTrailerMap& response_trailers,
                                 const StreamInfo::StreamInfo&, absl::string_view) const override;

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
  RequestHeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                         absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap& request_headers,
                                     const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view) const override;
};

/**
 * FormatterProvider for response headers.
 */
class ResponseHeaderFormatter : public FormatterProvider, HeaderFormatter {
public:
  ResponseHeaderFormatter(const std::string& main_header, const std::string& alternative_header,
                          absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap&,
                                     const Http::ResponseHeaderMap& response_headers,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view) const override;
};

/**
 * FormatterProvider for response trailers.
 */
class ResponseTrailerFormatter : public FormatterProvider, HeaderFormatter {
public:
  ResponseTrailerFormatter(const std::string& main_header, const std::string& alternative_header,
                           absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap& response_trailers,
                                     const StreamInfo::StreamInfo&,
                                     absl::string_view) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view) const override;
};

/**
 * FormatterProvider for grpc-status
 */
class GrpcStatusFormatter : public FormatterProvider, HeaderFormatter {
public:
  GrpcStatusFormatter(const std::string& main_header, const std::string& alternative_header,
                      absl::optional<size_t> max_length);

  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap&,
                                     const Http::ResponseHeaderMap& response_headers,
                                     const Http::ResponseTrailerMap& response_trailers,
                                     const StreamInfo::StreamInfo&,
                                     absl::string_view) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view) const override;
};

/**
 * FormatterProvider based on StreamInfo fields.
 */
class StreamInfoFormatter : public FormatterProvider {
public:
  StreamInfoFormatter(const std::string& field_name);

  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view) const override;

  class FieldExtractor {
  public:
    virtual ~FieldExtractor() = default;

    virtual absl::optional<std::string> extract(const StreamInfo::StreamInfo&) const PURE;
    virtual ProtobufWkt::Value extractValue(const StreamInfo::StreamInfo&) const PURE;
  };
  using FieldExtractorPtr = std::unique_ptr<FieldExtractor>;

  enum class StreamInfoAddressFieldExtractionType { WithPort, WithoutPort, JustPort };

private:
  FieldExtractorPtr field_extractor_;
};

/**
 * Base formatter for formatting Metadata objects
 */
class MetadataFormatter : public FormatterProvider {
public:
  using GetMetadataFunction =
      std::function<const envoy::config::core::v3::Metadata*(const StreamInfo::StreamInfo&)>;
  MetadataFormatter(const std::string& filter_namespace, const std::vector<std::string>& path,
                    absl::optional<size_t> max_length, GetMetadataFunction get);

  absl::optional<std::string> format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&,
                                     const StreamInfo::StreamInfo& stream_info,
                                     absl::string_view) const override;

  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&,
                                 const StreamInfo::StreamInfo& stream_info,
                                 absl::string_view) const override;

protected:
  absl::optional<std::string>
  formatMetadata(const envoy::config::core::v3::Metadata& metadata) const;
  ProtobufWkt::Value formatMetadataValue(const envoy::config::core::v3::Metadata& metadata) const;

private:
  std::string filter_namespace_;
  std::vector<std::string> path_;
  absl::optional<size_t> max_length_;
  GetMetadataFunction get_func_;
};

/**
 * FormatterProvider for DynamicMetadata from StreamInfo.
 */
class DynamicMetadataFormatter : public MetadataFormatter {
public:
  DynamicMetadataFormatter(const std::string& filter_namespace,
                           const std::vector<std::string>& path, absl::optional<size_t> max_length);
};

/**
 * FormatterProvider for ClusterMetadata from StreamInfo.
 */
class ClusterMetadataFormatter : public MetadataFormatter {
public:
  ClusterMetadataFormatter(const std::string& filter_namespace,
                           const std::vector<std::string>& path, absl::optional<size_t> max_length);
};

/**
 * FormatterProvider for FilterState from StreamInfo.
 */
class FilterStateFormatter : public FormatterProvider {
public:
  FilterStateFormatter(const std::string& key, absl::optional<size_t> max_length,
                       bool serialize_as_string);

  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view) const override;

private:
  const Envoy::StreamInfo::FilterState::Object*
  filterState(const StreamInfo::StreamInfo& stream_info) const;

  std::string key_;
  absl::optional<size_t> max_length_;

  bool serialize_as_string_;
};

/**
 * Base FormatterProvider for system times from StreamInfo.
 */
class SystemTimeFormatter : public FormatterProvider {
public:
  using TimeFieldExtractor =
      std::function<absl::optional<SystemTime>(const StreamInfo::StreamInfo& stream_info)>;
  using TimeFieldExtractorPtr = std::unique_ptr<TimeFieldExtractor>;

  SystemTimeFormatter(const std::string& format, TimeFieldExtractorPtr f);

  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view) const override;

protected:
  // Given an access log token, attempt to parse out the format string between parenthesis.
  //
  // @param token The access log token, e.g. `START_TIME` or `START_TIME(...)`
  // @param parameters_start The index of the first character where the parameters parenthesis would
  //                         begin if it exists. Must not be out of bounds of `token` or its NUL
  //                         char.
  // @return The format string between parenthesis, or an empty string if none exists.
  static std::string parseFormat(const std::string& token, size_t parameters_start);

private:
  const Envoy::DateFormatter date_formatter_;
  const TimeFieldExtractorPtr time_field_extractor_;
};

/**
 * SystemTimeFormatter (FormatterProvider) for request start time from StreamInfo.
 */
class StartTimeFormatter : public SystemTimeFormatter {
public:
  StartTimeFormatter(const std::string& format);
};

/**
 * SystemTimeFormatter (FormatterProvider) for downstream cert start time from the StreamInfo's
 * ConnectionInfo.
 */
class DownstreamPeerCertVStartFormatter : public SystemTimeFormatter {
public:
  DownstreamPeerCertVStartFormatter(const std::string& format);
};

/**
 * SystemTimeFormatter (FormatterProvider) for downstream cert end time from the StreamInfo's
 * ConnectionInfo.
 */
class DownstreamPeerCertVEndFormatter : public SystemTimeFormatter {
public:
  DownstreamPeerCertVEndFormatter(const std::string& format);
};

} // namespace Formatter
} // namespace Envoy
