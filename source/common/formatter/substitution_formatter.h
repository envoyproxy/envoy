#pragma once

#include <bitset>
#include <functional>
#include <list>
#include <regex>
#include <string>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/utility.h"
#include "source/common/formatter/http_formatter_context.h"
#include "source/common/json/json_loader.h"
#include "source/common/json/json_streamer.h"
#include "source/common/json/json_utility.h"

#include "absl/types/optional.h"
#include "re2/re2.h"

namespace Envoy {
namespace Formatter {

/**
 * FormatterProvider for string literals. It ignores headers and stream info and returns string by
 * which it was initialized.
 */
template <class FormatterContext>
class PlainStringFormatterBase : public FormatterProviderBase<FormatterContext> {
public:
  PlainStringFormatterBase(absl::string_view str) { str_.set_string_value(str); }

  // FormatterProviderBase
  absl::optional<std::string> formatWithContext(const FormatterContext&,
                                                const StreamInfo::StreamInfo&) const override {
    return str_.string_value();
  }
  ProtobufWkt::Value formatValueWithContext(const FormatterContext&,
                                            const StreamInfo::StreamInfo&) const override {
    return str_;
  }

private:
  ProtobufWkt::Value str_;
};

/**
 * FormatterProvider for numbers.
 */
template <class FormatterContext>
class PlainNumberFormatterBase : public FormatterProviderBase<FormatterContext> {
public:
  PlainNumberFormatterBase(double num) { num_.set_number_value(num); }

  // FormatterProviderBase
  absl::optional<std::string> formatWithContext(const FormatterContext&,
                                                const StreamInfo::StreamInfo&) const override {
    std::string str = absl::StrFormat("%g", num_.number_value());
    return str;
  }
  ProtobufWkt::Value formatValueWithContext(const FormatterContext&,
                                            const StreamInfo::StreamInfo&) const override {
    return num_;
  }

private:
  ProtobufWkt::Value num_;
};

/**
 * FormatterProvider based on StreamInfo fields.
 */
template <class FormatterContext>
class StreamInfoFormatterWrapper : public FormatterProviderBase<FormatterContext> {
public:
  StreamInfoFormatterWrapper(StreamInfoFormatterProviderPtr formatter)
      : formatter_(std::move(formatter)) {}

  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const FormatterContext&,
                    const StreamInfo::StreamInfo& stream_info) const override {
    return formatter_->format(stream_info);
  }
  ProtobufWkt::Value
  formatValueWithContext(const FormatterContext&,
                         const StreamInfo::StreamInfo& stream_info) const override {
    return formatter_->formatValue(stream_info);
  }

protected:
  StreamInfoFormatterProviderPtr formatter_;
};

/**
 * Access log format parser.
 */
class SubstitutionFormatParser {
public:
  template <class FormatterContext = HttpFormatterContext>
  static absl::StatusOr<std::vector<FormatterProviderBasePtr<FormatterContext>>>
  parse(absl::string_view format,
        const std::vector<CommandParserBasePtr<FormatterContext>>& command_parsers = {}) {
    std::string current_token;
    current_token.reserve(32);
    std::vector<FormatterProviderBasePtr<FormatterContext>> formatters;

    for (size_t pos = 0; pos < format.size();) {
      if (format[pos] != '%') {
        current_token.push_back(format[pos]);
        pos++;
        continue;
      }

      // escape '%%'
      if (format.size() > pos + 1) {
        if (format[pos + 1] == '%') {
          current_token.push_back('%');
          pos += 2;
          continue;
        }
      }

      if (!current_token.empty()) {
        formatters.emplace_back(FormatterProviderBasePtr<FormatterContext>{
            new PlainStringFormatterBase<FormatterContext>(current_token)});
        current_token.clear();
      }

      absl::string_view sub_format = format.substr(pos);
      const size_t sub_format_size = sub_format.size();

      absl::string_view command, command_arg;
      absl::optional<size_t> max_len;

      if (!re2::RE2::Consume(&sub_format, commandWithArgsRegex(), &command, &command_arg,
                             &max_len)) {
        return absl::InvalidArgumentError(
            fmt::format("Incorrect configuration: {}. Couldn't find valid command at position {}",
                        format, pos));
      }

      bool added = false;

      // The order of the following parsers is because the historical behavior. And we keep it
      // for backward compatibility.

      // First, try the built-in command parsers.
      for (const auto& cmd :
           BuiltInCommandParserFactoryHelper<FormatterContext>::commandParsers()) {
        auto formatter = cmd->parse(command, command_arg, max_len);
        if (formatter) {
          formatters.push_back(std::move(formatter));
          added = true;
          break;
        }
      }

      // Next, try the command parsers provided by the user.
      if (!added) {
        for (const auto& cmd : command_parsers) {
          auto formatter = cmd->parse(command, command_arg, max_len);
          if (formatter) {
            formatters.push_back(std::move(formatter));
            added = true;
            break;
          }
        }
      }

      // Finally, try the command parsers that are built-in and context-independent.
      if (!added) {
        for (const auto& cmd : BuiltInStreamInfoCommandParserFactoryHelper::commandParsers()) {
          auto formatter = cmd->parse(command, command_arg, max_len);
          if (formatter) {
            formatters.push_back(std::make_unique<StreamInfoFormatterWrapper<FormatterContext>>(
                std::move(formatter)));
            added = true;
            break;
          }
        }
      }

      if (!added) {
        return absl::InvalidArgumentError(
            fmt::format("Not supported field in StreamInfo: {}", command));
      }

      pos += (sub_format_size - sub_format.size());
    }

    if (!current_token.empty() || format.empty()) {
      // Create a PlainStringFormatter with the final string literal. If the format string
      // was empty, this creates a PlainStringFormatter with an empty string.
      formatters.emplace_back(FormatterProviderBasePtr<FormatterContext>{
          new PlainStringFormatterBase<FormatterContext>(current_token)});
    }

    return formatters;
  }

private:
  static const re2::RE2& commandWithArgsRegex();
};

inline constexpr absl::string_view DefaultUnspecifiedValueStringView = "-";

/**
 * Composite formatter implementation.
 */
template <class FormatterContext> class FormatterBaseImpl : public FormatterBase<FormatterContext> {
public:
  using CommandParsers = std::vector<CommandParserBasePtr<FormatterContext>>;

  static absl::StatusOr<std::unique_ptr<FormatterBaseImpl>>
  create(absl::string_view format, bool omit_empty_values = false,
         const CommandParsers& command_parsers = {}) {
    absl::Status creation_status = absl::OkStatus();
    auto ret = std::unique_ptr<FormatterBaseImpl>(
        new FormatterBaseImpl(creation_status, format, omit_empty_values, command_parsers));
    RETURN_IF_NOT_OK_REF(creation_status);
    return ret;
  }

  // FormatterBase
  std::string formatWithContext(const FormatterContext& context,
                                const StreamInfo::StreamInfo& stream_info) const override {
    std::string log_line;
    log_line.reserve(256);

    for (const auto& provider : providers_) {
      const auto bit = provider->formatWithContext(context, stream_info);
      log_line += bit.value_or(empty_value_string_);
    }

    return log_line;
  }

protected:
  FormatterBaseImpl(absl::Status& creation_status, absl::string_view format,
                    bool omit_empty_values = false)
      : empty_value_string_(omit_empty_values ? absl::string_view{}
                                              : DefaultUnspecifiedValueStringView) {
    auto providers_or_error = SubstitutionFormatParser::parse<FormatterContext>(format);
    SET_AND_RETURN_IF_NOT_OK(providers_or_error.status(), creation_status);
    providers_ = std::move(*providers_or_error);
  }
  FormatterBaseImpl(absl::Status& creation_status, absl::string_view format, bool omit_empty_values,
                    const CommandParsers& command_parsers = {})
      : empty_value_string_(omit_empty_values ? absl::string_view{}
                                              : DefaultUnspecifiedValueStringView) {
    auto providers_or_error =
        SubstitutionFormatParser::parse<FormatterContext>(format, command_parsers);
    SET_AND_RETURN_IF_NOT_OK(providers_or_error.status(), creation_status);
    providers_ = std::move(*providers_or_error);
  }

private:
  const std::string empty_value_string_;
  std::vector<FormatterProviderBasePtr<FormatterContext>> providers_;
};

// Helper class to write value to output buffer in JSON style.
// NOTE: This helper class has duplicated logic with the Json::BufferStreamer class but
// provides lower level of APIs to operate on the output buffer (like control the
// delimiters). This is designed for special scenario of substitution formatter and
// is not intended to be used by other parts of the code.
class JsonStringSerializer {
public:
  using OutputBufferType = Json::StringOutput;
  explicit JsonStringSerializer(std::string& output_buffer) : output_buffer_(output_buffer) {}

  // Methods that be used to add JSON delimiter to output buffer.
  void addMapBeginDelimiter() { output_buffer_.add(Json::Constants::MapBegin); }
  void addMapEndDelimiter() { output_buffer_.add(Json::Constants::MapEnd); }
  void addArrayBeginDelimiter() { output_buffer_.add(Json::Constants::ArrayBegin); }
  void addArrayEndDelimiter() { output_buffer_.add(Json::Constants::ArrayEnd); }
  void addElementsDelimiter() { output_buffer_.add(Json::Constants::Comma); }
  void addKeyValueDelimiter() { output_buffer_.add(Json::Constants::Colon); }

  // Methods that be used to add JSON key or value to output buffer.
  void addString(absl::string_view value) { addSanitized(R"(")", value, R"(")"); }
  /**
   * Serializes a number.
   */
  void addNumber(double d) {
    if (std::isnan(d)) {
      output_buffer_.add(Json::Constants::Null);
    } else {
      Buffer::Util::serializeDouble(d, output_buffer_);
    }
  }
  /**
   * Serializes a integer number.
   * NOTE: All numbers in JSON is float. When loading output of this serializer, the parser's
   * implementation decides if the full precision of big integer could be preserved or not.
   * See discussion here https://stackoverflow.com/questions/13502398/json-integers-limit-on-size
   * and spec https://www.rfc-editor.org/rfc/rfc7159#section-6 for more details.
   */
  void addNumber(uint64_t i) { output_buffer_.add(absl::StrCat(i)); }
  void addNumber(int64_t i) { output_buffer_.add(absl::StrCat(i)); }
  void addBool(bool b) { output_buffer_.add(b ? Json::Constants::True : Json::Constants::False); }
  void addNull() { output_buffer_.add(Json::Constants::Null); }

  // Low-level methods that be used to provide a low-level control to buffer.
  void addSanitized(absl::string_view prefix, absl::string_view value, absl::string_view suffix) {
    output_buffer_.add(prefix, Json::sanitize(sanitize_buffer_, value), suffix);
  }
  void addRawString(absl::string_view value) { output_buffer_.add(value); }

protected:
  std::string sanitize_buffer_;
  OutputBufferType output_buffer_;
};

// Helper class to parse the Json format configuration. The class will be used to parse
// the JSON format configuration and convert it to a list of raw JSON pieces and
// substitution format template strings. See comments below for more details.
class JsonFormatBuilder {
public:
  struct FormatElement {
    // Pre-sanitized JSON piece or a format template string that contains
    // substitution commands.
    std::string value_;
    // Whether the value is a template string.
    // If true, the value is a format template string that contains substitution commands.
    // If false, the value is a pre-sanitized JSON piece.
    bool is_template_;
  };
  using FormatElements = std::vector<FormatElement>;

  /**
   * Constructor of JsonFormatBuilder.
   */
  JsonFormatBuilder() = default;

  /**
   * Convert a proto struct format configuration to an array of raw JSON pieces and
   * substitution format template strings.
   *
   * The keys, raw values, delimiters will be serialized as JSON string pieces (raw
   * JSON strings) directly when loading the configuration.
   * The substitution format template strings will be kept as template string pieces and
   * will be parsed to formatter providers by the JsonFormatter.
   *
   * NOTE: This class is used to parse the configuration of the proto struct format
   * and should only be used in the context of parsing the configuration.
   *
   * For example given the following proto struct format configuration:
   *
   *   json_format:
   *     name: "value"
   *     template: "%START_TIME%"
   *     number: 2
   *     bool: true
   *     list:
   *       - "list_raw_value"
   *       - false
   *       - "%EMIT_TIME%"
   *     nested:
   *       nested_name: "nested_value"
   *
   * It will be parsed to the following pieces:
   *
   *   - '{"name":"value","template":'                                      # Raw JSON piece.
   *   - '%START_TIME%'                                                     # Format template piece.
   *   - ',"number":2,"bool":true,"list":["list_raw_value",false,'          # Raw JSON piece.
   *   - '%EMIT_TIME%'                                                      # Format template piece.
   *   - '],"nested":{"nested_name":"nested_value"}}'                       # Raw JSON piece.
   *
   * Finally, join the raw JSON pieces and output of substitution formatters in order
   * to construct the final JSON output.
   *
   * @param struct_format the proto struct format configuration.
   */
  FormatElements fromStruct(const ProtobufWkt::Struct& struct_format);

private:
  using ProtoDict = Protobuf::Map<std::string, ProtobufWkt::Value>;
  using ProtoList = Protobuf::RepeatedPtrField<ProtobufWkt::Value>;

  void formatValueToFormatElements(const ProtoDict& dict_value);
  void formatValueToFormatElements(const ProtobufWkt::Value& value);
  void formatValueToFormatElements(const ProtoList& list_value);

  std::string buffer_;                       // JSON writer buffer.
  JsonStringSerializer serializer_{buffer_}; // JSON serializer.
  FormatElements elements_;                  // Parsed elements.
};

template <class FormatterContext>
class JsonFormatterImplBase : public FormatterBase<FormatterContext> {
public:
  using CommandParsers = std::vector<CommandParserBasePtr<FormatterContext>>;
  using Formatter = FormatterProviderBasePtr<FormatterContext>;
  using Formatters = std::vector<Formatter>;

  JsonFormatterImplBase(const ProtobufWkt::Struct& struct_format, bool omit_empty_values,
                        const CommandParsers& commands = {})
      : empty_value_(omit_empty_values ? std::string()
                                       : std::string(DefaultUnspecifiedValueStringView)) {

    for (JsonFormatBuilder::FormatElement& element :
         JsonFormatBuilder().fromStruct(struct_format)) {
      if (element.is_template_) {
        parsed_elements_.emplace_back(THROW_OR_RETURN_VALUE(
            SubstitutionFormatParser::parse<FormatterContext>(element.value_, commands),
            std::vector<FormatterProviderBasePtr<FormatterContext>>));
      } else {
        parsed_elements_.emplace_back(std::move(element.value_));
      }
    }
  }

  std::string formatWithContext(const FormatterContext& context,
                                const StreamInfo::StreamInfo& info) const override {
    std::string log_line;
    log_line.reserve(2048);
    JsonStringSerializer serializer(log_line); // Helper to serialize the value to log line.

    for (const ParsedFormatElement& element : parsed_elements_) {
      // 1. Handle the raw string element.
      if (absl::holds_alternative<std::string>(element)) {
        // The raw string element will be added to the buffer directly.
        // It is sanitized when loading the configuration.
        serializer.addRawString(absl::get<std::string>(element));
        continue;
      }

      ASSERT(absl::holds_alternative<Formatters>(element));
      const Formatters& formatters = absl::get<Formatters>(element);
      ASSERT(!formatters.empty());

      if (formatters.size() != 1) {
        // 2. Handle the formatter element with multiple or zero providers.
        stringValueToLogLine(formatters, context, info, serializer);
      } else {
        // 3. Handle the formatter element with a single provider and value
        //    type needs to be kept.
        auto value = formatters[0]->formatValueWithContext(context, info);
        Json::Utility::appendValueToString(value, log_line);
      }
    }

    log_line.push_back('\n');
    return log_line;
  }

private:
  void stringValueToLogLine(const Formatters& formatters, const FormatterContext& context,
                            const StreamInfo::StreamInfo& info,
                            JsonStringSerializer& serializer) const {

    serializer.addRawString(Json::Constants::DoubleQuote); // Start the JSON string.
    for (const Formatter& formatter : formatters) {
      const absl::optional<std::string> value = formatter->formatWithContext(context, info);
      if (!value.has_value()) {
        // Add the empty value. This needn't be sanitized.
        serializer.addRawString(empty_value_);
        continue;
      }
      // Sanitize the string value and add it to the buffer. The string value will not be quoted
      // since we handle the quoting by ourselves at the outer level.
      serializer.addSanitized({}, value.value(), {});
    }
    serializer.addRawString(Json::Constants::DoubleQuote); // End the JSON string.
  }

  const std::string empty_value_;

  using ParsedFormatElement = absl::variant<std::string, Formatters>;
  std::vector<ParsedFormatElement> parsed_elements_;
};

using JsonFormatterImpl = JsonFormatterImplBase<HttpFormatterContext>;

// Helper classes for StructFormatter::StructFormatMapVisitor.
template <class... Ts> struct StructFormatMapVisitorHelper : Ts... { using Ts::operator()...; };
template <class... Ts> StructFormatMapVisitorHelper(Ts...) -> StructFormatMapVisitorHelper<Ts...>;

#ifndef ENVOY_DISABLE_EXCEPTIONS
/**
 * An formatter for structured log formats, which returns a Struct proto that
 * can be converted easily into multiple formats.
 */
template <class FormatterContext> class StructFormatterBase {
public:
  using CommandParsers = std::vector<CommandParserBasePtr<FormatterContext>>;
  using PlainNumber = PlainNumberFormatterBase<FormatterContext>;
  using PlainString = PlainStringFormatterBase<FormatterContext>;

  StructFormatterBase(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                      bool omit_empty_values, const CommandParsers& commands = {})
      : omit_empty_values_(omit_empty_values), preserve_types_(preserve_types),
        empty_value_(omit_empty_values_ ? std::string()
                                        : std::string(DefaultUnspecifiedValueStringView)),
        struct_output_format_(FormatBuilder(commands).toFormatMapValue(format_mapping)) {}

  ProtobufWkt::Struct formatWithContext(const FormatterContext& context,
                                        const StreamInfo::StreamInfo& info) const {
    StructFormatMapVisitor visitor{
        [&](const std::vector<FormatterProviderBasePtr<FormatterContext>>& providers) {
          return providersCallback(providers, context, info);
        },
        [&, this](const StructFormatterBase::StructFormatMapWrapper& format_map) {
          return structFormatMapCallback(format_map, visitor);
        },
        [&, this](const StructFormatterBase::StructFormatListWrapper& format_list) {
          return structFormatListCallback(format_list, visitor);
        },
    };
    return structFormatMapCallback(struct_output_format_, visitor).struct_value();
  }

private:
  struct StructFormatMapWrapper;
  struct StructFormatListWrapper;
  using StructFormatValue =
      absl::variant<const std::vector<FormatterProviderBasePtr<FormatterContext>>,
                    const StructFormatMapWrapper, const StructFormatListWrapper>;
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
      const std::function<ProtobufWkt::Value(
          const std::vector<FormatterProviderBasePtr<FormatterContext>>&)>,
      const std::function<ProtobufWkt::Value(const StructFormatterBase::StructFormatMapWrapper&)>,
      const std::function<ProtobufWkt::Value(const StructFormatterBase::StructFormatListWrapper&)>>;

  // Methods for building the format map.
  class FormatBuilder {
  public:
    explicit FormatBuilder(const CommandParsers& commands) : commands_(commands) {}
    absl::StatusOr<std::vector<FormatterProviderBasePtr<FormatterContext>>>
    toFormatStringValue(const std::string& string_format) const {
      return SubstitutionFormatParser::parse<FormatterContext>(string_format, commands_);
    }
    std::vector<FormatterProviderBasePtr<FormatterContext>>
    toFormatNumberValue(double value) const {
      std::vector<FormatterProviderBasePtr<FormatterContext>> formatters;
      formatters.emplace_back(FormatterProviderBasePtr<FormatterContext>{new PlainNumber(value)});
      return formatters;
    }
    StructFormatMapWrapper toFormatMapValue(const ProtobufWkt::Struct& struct_format) const {
      auto output = std::make_unique<StructFormatMap>();
      for (const auto& pair : struct_format.fields()) {
        switch (pair.second.kind_case()) {
        case ProtobufWkt::Value::kStringValue:
          output->emplace(pair.first, THROW_OR_RETURN_VALUE(
                                          toFormatStringValue(pair.second.string_value()),
                                          std::vector<FormatterProviderBasePtr<FormatterContext>>));
          break;

        case ProtobufWkt::Value::kStructValue:
          output->emplace(pair.first, toFormatMapValue(pair.second.struct_value()));
          break;

        case ProtobufWkt::Value::kListValue:
          output->emplace(pair.first, toFormatListValue(pair.second.list_value()));
          break;

        case ProtobufWkt::Value::kNumberValue:
          output->emplace(pair.first, toFormatNumberValue(pair.second.number_value()));
          break;
        default:
          throw EnvoyException(
              "Only string values, nested structs, list values and number values are "
              "supported in structured access log format.");
        }
      }
      return {std::move(output)};
    }
    StructFormatListWrapper
    toFormatListValue(const ProtobufWkt::ListValue& list_value_format) const {
      auto output = std::make_unique<StructFormatList>();
      for (const auto& value : list_value_format.values()) {
        switch (value.kind_case()) {
        case ProtobufWkt::Value::kStringValue:
          output->emplace_back(
              THROW_OR_RETURN_VALUE(toFormatStringValue(value.string_value()),
                                    std::vector<FormatterProviderBasePtr<FormatterContext>>));
          break;

        case ProtobufWkt::Value::kStructValue:
          output->emplace_back(toFormatMapValue(value.struct_value()));
          break;

        case ProtobufWkt::Value::kListValue:
          output->emplace_back(toFormatListValue(value.list_value()));
          break;

        case ProtobufWkt::Value::kNumberValue:
          output->emplace_back(toFormatNumberValue(value.number_value()));
          break;

        default:
          throw EnvoyException(
              "Only string values, nested structs, list values and number values are "
              "supported in structured access log format.");
        }
      }
      return {std::move(output)};
    }

  private:
    const CommandParsers& commands_;
  };

  // Methods for doing the actual formatting.
  ProtobufWkt::Value
  providersCallback(const std::vector<FormatterProviderBasePtr<FormatterContext>>& providers,
                    const FormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const {
    ASSERT(!providers.empty());
    if (providers.size() == 1) {
      const auto& provider = providers.front();
      if (preserve_types_) {
        return provider->formatValueWithContext(context, stream_info);
      }

      if (omit_empty_values_) {
        return ValueUtil::optionalStringValue(provider->formatWithContext(context, stream_info));
      }

      const auto str = provider->formatWithContext(context, stream_info);
      return ValueUtil::stringValue(str.value_or(empty_value_));
    }
    // Multiple providers forces string output.
    std::string str;
    for (const auto& provider : providers) {
      const auto bit = provider->formatWithContext(context, stream_info);
      str += bit.value_or(empty_value_);
    }
    return ValueUtil::stringValue(str);
  }
  ProtobufWkt::Value
  structFormatMapCallback(const StructFormatterBase::StructFormatMapWrapper& format_map,
                          const StructFormatMapVisitor& visitor) const {
    ProtobufWkt::Struct output;
    auto* fields = output.mutable_fields();
    for (const auto& pair : *format_map.value_) {
      ProtobufWkt::Value value = absl::visit(visitor, pair.second);
      if (omit_empty_values_ && value.kind_case() == ProtobufWkt::Value::kNullValue) {
        continue;
      }
      (*fields)[pair.first] = value;
    }
    if (omit_empty_values_ && output.fields().empty()) {
      return ValueUtil::nullValue();
    }
    return ValueUtil::structValue(output);
  }
  ProtobufWkt::Value
  structFormatListCallback(const StructFormatterBase::StructFormatListWrapper& format_list,
                           const StructFormatMapVisitor& visitor) const {
    std::vector<ProtobufWkt::Value> output;
    for (const auto& val : *format_list.value_) {
      ProtobufWkt::Value value = absl::visit(visitor, val);
      if (omit_empty_values_ && value.kind_case() == ProtobufWkt::Value::kNullValue) {
        continue;
      }
      output.push_back(value);
    }
    return ValueUtil::listValue(output);
  }

  const bool omit_empty_values_;
  const bool preserve_types_;
  const std::string empty_value_;

  const StructFormatMapWrapper struct_output_format_;
};

template <class FormatterContext>
using StructFormatterBasePtr = std::unique_ptr<StructFormatterBase<FormatterContext>>;

template <class FormatterContext>
class LegacyJsonFormatterBaseImpl : public FormatterBase<FormatterContext> {
public:
  using CommandParsers = std::vector<CommandParserBasePtr<FormatterContext>>;

  LegacyJsonFormatterBaseImpl(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                              bool omit_empty_values, bool sort_properties,
                              const CommandParsers& commands = {})
      : struct_formatter_(format_mapping, preserve_types, omit_empty_values, commands),
        sort_properties_(sort_properties) {}

  // FormatterBase
  std::string formatWithContext(const FormatterContext& context,
                                const StreamInfo::StreamInfo& info) const override {
    const ProtobufWkt::Struct output_struct = struct_formatter_.formatWithContext(context, info);

    std::string log_line = "";
#ifdef ENVOY_ENABLE_YAML
    if (sort_properties_) {
      log_line = Json::Factory::loadFromProtobufStruct(output_struct)->asJsonString();
    } else {
      log_line = MessageUtil::getJsonStringFromMessageOrError(output_struct, false, true);
    }
#else
    UNREFERENCED_PARAMETER(sort_properties_);
    IS_ENVOY_BUG("Json support compiled out");
#endif
    return absl::StrCat(log_line, "\n");
  }

private:
  const StructFormatterBase<FormatterContext> struct_formatter_;
  const bool sort_properties_;
};

using StructFormatter = StructFormatterBase<HttpFormatterContext>;
using StructFormatterPtr = std::unique_ptr<StructFormatter>;
using LegacyJsonFormatterImpl = LegacyJsonFormatterBaseImpl<HttpFormatterContext>;
#endif // ENVOY_DISABLE_EXCEPTIONS

// Aliases for backwards compatibility.
using FormatterImpl = FormatterBaseImpl<HttpFormatterContext>;

} // namespace Formatter
} // namespace Envoy
