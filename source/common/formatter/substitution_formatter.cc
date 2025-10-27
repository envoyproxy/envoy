#include "source/common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Formatter {

const re2::RE2& commandWithArgsRegex() {
  // The following regex is used to check validity of the formatter command and to
  // extract groups.
  // The formatter command has the following format:
  //    % COMMAND(SUBCOMMAND):LENGTH%
  // % signs at the beginning and end are used by parser to find next COMMAND.
  // COMMAND must always be present and must consist of characters: "A-Z", "0-9" or "_".
  // SUBCOMMAND presence depends on the COMMAND. Format is flexible but cannot contain ")".:
  // - for some commands SUBCOMMAND is not allowed (for example %PROTOCOL%)
  // - for some commands SUBCOMMAND is required (for example %REQ(:AUTHORITY)%, just %REQ% will
  // cause error)
  // - for some commands SUBCOMMAND is optional (for example %START_TIME% and
  // %START_TIME(%f.%1f.%2f.%3f)% are both correct).
  // LENGTH presence depends on the command. Some
  // commands allow LENGTH to be specified, so not. Regex is used to validate the syntax and also
  // to extract values for COMMAND, SUBCOMMAND and LENGTH.
  //
  // Below is explanation of capturing and non-capturing groups. Non-capturing groups are used
  // to specify that certain part of the formatter command is optional and should contain specific
  // characters. Capturing groups are used to extract the values when regex is matched against
  // formatter command string.
  //
  // clang-format off
  // Non-capturing group specifying optional :LENGTH ----------------------
  //                                                                       |
  // Non-capturing group specifying optional (SUBCOMMAND)---               |
  //                                                        |              |
  // Non-capturing group specifying mandatory COMMAND       |              |
  //  which uses only A-Z, 0-9 and _ characters             |              |
  //  Group is used only to specify allowed characters.     |              |
  //                                      |                 |              |
  //                                      |                 |              |
  //                              _________________  _____________ _____________
  //                              |               |  |           | |           |
  CONSTRUCT_ON_FIRST_USE(re2::RE2,
                         R"EOF(^%((?:[A-Z]|[0-9]|_)+)(?:\((.*?)\))?(?::([0-9]+))?%)EOF");
  //                             |__________________|     |___|        |______|
  //                                       |                |              |
  // Capturing group specifying COMMAND ---                 |              |
  // The index of this group is 1.                          |              |
  //                                                        |              |
  // Capturing group for SUBCOMMAND. If present, it will ---               |
  // contain SUBCOMMAND without "(" and ")". The index                     |
  // of SUBCOMMAND group is 2.                                             |
  //                                                                       |
  // Capturing group for LENGTH. If present, it will ----------------------
  // contain just number without ":". The index of
  // LENGTH group is 3.
  // clang-format on
}

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
  FormatElements fromStruct(const Protobuf::Struct& struct_format);

private:
  using ProtoDict = Protobuf::Map<std::string, Protobuf::Value>;
  using ProtoList = Protobuf::RepeatedPtrField<Protobuf::Value>;

  void formatValueToFormatElements(const ProtoDict& dict_value);
  void formatValueToFormatElements(const Protobuf::Value& value);
  void formatValueToFormatElements(const ProtoList& list_value);

  std::string buffer_;                       // JSON writer buffer.
  JsonStringSerializer serializer_{buffer_}; // JSON serializer.
  FormatElements elements_;                  // Parsed elements.
};

JsonFormatBuilder::FormatElements
JsonFormatBuilder::fromStruct(const Protobuf::Struct& struct_format) {
  elements_.clear();

  // This call will iterate through the map tree and serialize the key/values as JSON.
  // If a string value that contains a substitution commands is found, the current
  // JSON piece and the substitution command will be pushed into the output list.
  // After that, the iteration will continue until the whole tree is traversed.
  formatValueToFormatElements(struct_format.fields());
  elements_.push_back(FormatElement{std::move(buffer_), false});
  buffer_.clear();

  return std::move(elements_);
};

void JsonFormatBuilder::formatValueToFormatElements(const Protobuf::Value& value) {
  switch (value.kind_case()) {
  case Protobuf::Value::KIND_NOT_SET:
  case Protobuf::Value::kNullValue:
    serializer_.addNull();
    break;
  case Protobuf::Value::kNumberValue:
    serializer_.addNumber(value.number_value());
    break;
  case Protobuf::Value::kStringValue: {
    absl::string_view string_format = value.string_value();
    if (!absl::StrContains(string_format, '%')) {
      serializer_.addString(string_format);
      break;
    }

    // The string contains a formatter, we need to push the current exist JSON piece
    // into the output list first.
    elements_.push_back(FormatElement{std::move(buffer_), false});
    buffer_.clear();

    // Now a formatter is coming, we need to push the current raw string into
    // the output list.
    elements_.push_back(FormatElement{std::string(string_format), true});
    break;
  }
  case Protobuf::Value::kBoolValue:
    serializer_.addBool(value.bool_value());
    break;
  case Protobuf::Value::kStructValue: {
    formatValueToFormatElements(value.struct_value().fields());
    break;
  case Protobuf::Value::kListValue:
    formatValueToFormatElements(value.list_value().values());
    break;
  }
  }
}

void JsonFormatBuilder::formatValueToFormatElements(const ProtoList& list_value) {
  serializer_.addArrayBeginDelimiter(); // Delimiter to start list.
  for (int i = 0; i < list_value.size(); ++i) {
    if (i > 0) {
      serializer_.addElementsDelimiter(); // Delimiter to separate list elements.
    }
    formatValueToFormatElements(list_value[i]);
  }
  serializer_.addArrayEndDelimiter(); // Delimiter to end list.
}

void JsonFormatBuilder::formatValueToFormatElements(const ProtoDict& dict_value) {
  std::vector<std::pair<absl::string_view, ProtoDict::const_iterator>> sorted_fields;
  sorted_fields.reserve(dict_value.size());

  for (auto it = dict_value.begin(); it != dict_value.end(); ++it) {
    sorted_fields.push_back({it->first, it});
  }

  // Sort the keys to make the output deterministic.
  std::sort(sorted_fields.begin(), sorted_fields.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  serializer_.addMapBeginDelimiter(); // Delimiter to start map.
  for (size_t i = 0; i < sorted_fields.size(); ++i) {
    if (i > 0) {
      serializer_.addElementsDelimiter(); // Delimiter to separate map elements.
    }
    // Add the key.
    serializer_.addString(sorted_fields[i].first);
    serializer_.addKeyValueDelimiter(); // Delimiter to separate key and value.
    formatValueToFormatElements(sorted_fields[i].second->second);
  }
  serializer_.addMapEndDelimiter(); // Delimiter to end map.
}

absl::StatusOr<std::vector<FormatterProviderPtr>>
SubstitutionFormatParser::parse(absl::string_view format,
                                const std::vector<CommandParserPtr>& command_parsers) {
  std::string current_token;
  current_token.reserve(32);
  std::vector<FormatterProviderPtr> formatters;

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
      formatters.emplace_back(FormatterProviderPtr{new PlainStringFormatter(current_token)});
      current_token.clear();
    }

    absl::string_view sub_format = format.substr(pos);
    const size_t sub_format_size = sub_format.size();

    absl::string_view command, command_arg;
    absl::optional<size_t> max_len;

    if (!re2::RE2::Consume(&sub_format, commandWithArgsRegex(), &command, &command_arg, &max_len)) {
      return absl::InvalidArgumentError(fmt::format(
          "Incorrect configuration: {}. Couldn't find valid command at position {}", format, pos));
    }

    bool added = false;

    // First try the command parsers provided by the user. This allows the user to override
    // built-in command parsers.
    for (const auto& cmd : command_parsers) {
      auto formatter = cmd->parse(command, command_arg, max_len);
      if (formatter) {
        formatters.push_back(std::move(formatter));
        added = true;
        break;
      }
    }

    // Next, try the built-in command parsers.
    if (!added) {
      for (const auto& cmd : BuiltInCommandParserFactoryHelper::commandParsers()) {
        auto formatter = cmd->parse(command, command_arg, max_len);
        if (formatter) {
          formatters.push_back(std::move(formatter));
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
    formatters.emplace_back(FormatterProviderPtr{new PlainStringFormatter(current_token)});
  }

  return formatters;
}

absl::StatusOr<std::unique_ptr<FormatterImpl>>
FormatterImpl::create(absl::string_view format, bool omit_empty_values,
                      const CommandParsers& command_parsers) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<FormatterImpl>(
      new FormatterImpl(creation_status, format, omit_empty_values, command_parsers));
  RETURN_IF_NOT_OK_REF(creation_status);
  return ret;
}

std::string FormatterImpl::format(const Context& context,
                                  const StreamInfo::StreamInfo& stream_info) const {
  std::string log_line;
  log_line.reserve(256);

  for (const auto& provider : providers_) {
    const absl::optional<std::string> bit = provider->format(context, stream_info);
    // Add the formatted value if there is one. Otherwise add a default value
    // of "-" if omit_empty_values_ is not set.
    if (bit.has_value()) {
      log_line += bit.value();
    } else if (!omit_empty_values_) {
      log_line += DefaultUnspecifiedValueStringView;
    }
  }

  return log_line;
}

void stringValueToLogLine(const JsonFormatterImpl::Formatters& formatters, const Context& context,
                          const StreamInfo::StreamInfo& info, std::string& log_line,
                          std::string& sanitize, bool omit_empty_values) {
  log_line.push_back('"'); // Start the JSON string.
  for (const JsonFormatterImpl::Formatter& formatter : formatters) {
    const absl::optional<std::string> value = formatter->format(context, info);
    if (!value.has_value()) {
      // Add the empty value. This needn't be sanitized.
      log_line.append(omit_empty_values ? EMPTY_STRING : DefaultUnspecifiedValueStringView);
      continue;
    }
    // Sanitize the string value and add it to the buffer. The string value will not be quoted
    // since we handle the quoting by ourselves at the outer level.
    log_line.append(Json::sanitize(sanitize, value.value()));
  }
  log_line.push_back('"'); // End the JSON string.
}

JsonFormatterImpl::JsonFormatterImpl(const Protobuf::Struct& struct_format, bool omit_empty_values,
                                     const CommandParsers& commands)
    : omit_empty_values_(omit_empty_values) {
  for (JsonFormatBuilder::FormatElement& element : JsonFormatBuilder().fromStruct(struct_format)) {
    if (element.is_template_) {
      parsed_elements_.emplace_back(
          THROW_OR_RETURN_VALUE(SubstitutionFormatParser::parse(element.value_, commands),
                                std::vector<FormatterProviderPtr>));
    } else {
      parsed_elements_.emplace_back(std::move(element.value_));
    }
  }
}

std::string JsonFormatterImpl::format(const Context& context,
                                      const StreamInfo::StreamInfo& info) const {
  std::string log_line;
  log_line.reserve(2048);
  std::string sanitize; // Helper to serialize the value to log line.

  for (const ParsedFormatElement& element : parsed_elements_) {
    // 1. Handle the raw string element.
    if (absl::holds_alternative<std::string>(element)) {
      // The raw string element will be added to the buffer directly.
      // It is sanitized when loading the configuration.
      log_line.append(absl::get<std::string>(element));
      continue;
    }

    ASSERT(absl::holds_alternative<Formatters>(element));
    const Formatters& formatters = absl::get<Formatters>(element);
    ASSERT(!formatters.empty());

    if (formatters.size() != 1) {
      // 2. Handle the formatter element with multiple or zero providers.
      stringValueToLogLine(formatters, context, info, log_line, sanitize, omit_empty_values_);
    } else {
      // 3. Handle the formatter element with a single provider and value
      //    type needs to be kept.
      const auto value = formatters[0]->formatValue(context, info);
      Json::Utility::appendValueToString(value, log_line);
    }
  }

  log_line.push_back('\n');
  return log_line;
}

} // namespace Formatter
} // namespace Envoy
