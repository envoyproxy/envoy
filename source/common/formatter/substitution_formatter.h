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
#include "source/common/formatter/http_specific_formatter.h"
#include "source/common/formatter/stream_info_formatter.h"

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
                    bool omit_empty_values, bool sort_properties)
      : struct_formatter_(format_mapping, preserve_types, omit_empty_values),
        sort_properties_(sort_properties) {}
  JsonFormatterImpl(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                    bool omit_empty_values, bool sort_properties,
                    const std::vector<CommandParserPtr>& commands)
      : struct_formatter_(format_mapping, preserve_types, omit_empty_values, commands),
        sort_properties_(sort_properties) {}

  // Formatter::format
  std::string format(const Http::RequestHeaderMap& request_headers,
                     const Http::ResponseHeaderMap& response_headers,
                     const Http::ResponseTrailerMap& response_trailers,
                     const StreamInfo::StreamInfo& stream_info, absl::string_view local_reply_body,
                     AccessLog::AccessLogType access_log_type) const override;

private:
  const StructFormatter struct_formatter_;
  bool sort_properties_{false};
};

} // namespace Formatter
} // namespace Envoy
