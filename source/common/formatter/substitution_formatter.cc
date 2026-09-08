#include "source/common/formatter/substitution_formatter.h"

#include "source/common/formatter/builtin_command_parser_factory_helper.h"
#include "source/common/formatter/serializer.h"

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

namespace {

// Returns iterators to the fields of a JSON struct, sorted by key so that the serialized output is
// deterministic regardless of the map's internal ordering.
std::vector<Protobuf::Map<std::string, Protobuf::Value>::const_iterator>
sortJsonStructFields(const Protobuf::Map<std::string, Protobuf::Value>& fields) {
  std::vector<Protobuf::Map<std::string, Protobuf::Value>::const_iterator> sorted_fields;
  sorted_fields.reserve(fields.size());
  for (auto it = fields.begin(); it != fields.end(); ++it) {
    sorted_fields.push_back(it);
  }
  std::sort(sorted_fields.begin(), sorted_fields.end(),
            [](const auto& a, const auto& b) { return a->first < b->first; });
  return sorted_fields;
}

} // namespace

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
  const auto sorted_fields = sortJsonStructFields(dict_value);

  serializer_.addMapBeginDelimiter(); // Delimiter to start map.
  for (size_t i = 0; i < sorted_fields.size(); ++i) {
    if (i > 0) {
      serializer_.addElementsDelimiter(); // Delimiter to separate map elements.
    }
    // Add the key.
    serializer_.addString(sorted_fields[i]->first);
    serializer_.addKeyValueDelimiter(); // Delimiter to separate key and value.
    formatValueToFormatElements(sorted_fields[i]->second);
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
    std::optional<size_t> max_len;

    if (!re2::RE2::Consume(&sub_format, commandWithArgsRegex(), &command, &command_arg, &max_len)) {
      return absl::InvalidArgumentError(fmt::format(
          "Incorrect configuration: {}. Couldn't find valid command at position {}", format, pos));
    }

    bool added = false;

    // First try the command parsers provided by the user. This allows the user to override
    // built-in command parsers.
    for (const auto& cmd : command_parsers) {
      absl::StatusOr<FormatterProviderPtr> formatter_result =
          cmd->parse(command, command_arg, max_len);
      RETURN_IF_ERROR(formatter_result.status());
      FormatterProviderPtr formatter = std::move(formatter_result).value();
      if (formatter) {
        formatters.push_back(std::move(formatter));
        added = true;
        break;
      }
    }

    // Next, try the built-in command parsers.
    if (!added) {
      for (const auto& cmd : BuiltInCommandParserFactoryHelper::commandParsers()) {
        absl::StatusOr<FormatterProviderPtr> formatter_result =
            cmd->parse(command, command_arg, max_len);
        RETURN_IF_ERROR(formatter_result.status());
        FormatterProviderPtr formatter = std::move(formatter_result).value();
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
  log_line.reserve(constant_value_.has_value() ? constant_value_->size() : 256);
  formatTo(log_line, context, stream_info);
  return log_line;
}

void FormatterImpl::formatTo(std::string& sink, const Context& context,
                             const StreamInfo::StreamInfo& stream_info) const {
  if (constant_value_.has_value()) {
    sink.append(*constant_value_);
    return;
  }

  for (const auto& provider : providers_) {
    // Add the formatted value if there is one. Otherwise add a default value
    // of "-" if omit_empty_values_ is not set.
    if (!provider->formatTo(sink, context, stream_info) && !omit_empty_values_) {
      sink.append(DefaultUnspecifiedValueStringView);
    }
  }
}

void stringValueToLogLine(const JsonFormatterImpl::Formatters& formatters, const Context& context,
                          const StreamInfo::StreamInfo& info, JsonStringSerializer& output,
                          absl::string_view empty_value, std::string& scratch) {
  output.addStringBeginDelimiter(); // Start the JSON string.
  for (const JsonFormatterImpl::Formatter& formatter : formatters) {
    // 'value' is owned by the caller and reused for every provider of every field in the line,
    // so formatting a value stops allocating once it has grown to fit the widest one.
    scratch.clear();
    if (!formatter->formatTo(scratch, context, info)) {
      // Add the empty value. This needn't be sanitized.
      output.addRawString(empty_value);
      continue;
    }
    // Sanitize the string value and add it to the scratch buffer. The string value will not be
    // quoted since we handle the quoting by ourselves at the outer level.
    output.addSanitized(scratch);
  }
  output.addStringEndDelimiter(); // End the JSON string.
}

absl::StatusOr<std::unique_ptr<JsonFormatterImpl>>
JsonFormatterImpl::create(const Protobuf::Struct& struct_format, bool omit_empty_values,
                          const CommandParsers& commands) {
  std::vector<ParsedFormatElement> parsed_elements;
  for (JsonFormatBuilder::FormatElement& element : JsonFormatBuilder().fromStruct(struct_format)) {
    if (element.is_template_) {
      absl::StatusOr<std::vector<FormatterProviderPtr>> providers_or =
          SubstitutionFormatParser::parse(element.value_, commands);
      RETURN_IF_NOT_OK_REF(providers_or.status());
      parsed_elements.emplace_back(std::move(providers_or).value());
    } else {
      parsed_elements.emplace_back(std::move(element.value_));
    }
  }
  return std::make_unique<JsonFormatterImpl>(omit_empty_values, std::move(parsed_elements));
}

JsonFormatterImpl::JsonFormatterImpl(bool omit_empty_values,
                                     std::vector<ParsedFormatElement>&& parsed_elements)
    : omit_empty_values_(omit_empty_values), parsed_elements_(std::move(parsed_elements)) {}

std::string JsonFormatterImpl::format(const Context& context,
                                      const StreamInfo::StreamInfo& info) const {
  std::string log_line;
  log_line.reserve(2048);
  formatTo(log_line, context, info);
  return log_line;
}

void JsonFormatterImpl::formatTo(std::string& sink, const Context& context,
                                 const StreamInfo::StreamInfo& info) const {
  JsonStringSerializer output(sink);
  const absl::string_view empty_value =
      omit_empty_values_ ? EMPTY_STRING : DefaultUnspecifiedValueStringView;

  std::string scratch; // Helper to hold the formatted value buffer of a single provider.

  for (const ParsedFormatElement& element : parsed_elements_) {
    // 1. Handle the raw string element.
    if (absl::holds_alternative<std::string>(element)) {
      // The raw string element will be added to the buffer directly.
      // It is sanitized when loading the configuration.
      sink.append(absl::get<std::string>(element));
      continue;
    }

    ASSERT(absl::holds_alternative<Formatters>(element));
    const Formatters& formatters = absl::get<Formatters>(element);
    ASSERT(!formatters.empty());

    if (formatters.size() != 1) {
      // 2. Handle the formatter element with multiple or zero providers.
      stringValueToLogLine(formatters, context, info, output, empty_value, scratch);
    } else {
      // 3. Handle the formatter element with a single provider and value
      //    type needs to be kept.
      ValueSink sink_adapter(output);
      formatters[0]->formatValueTo(sink_adapter, context, info);
      if (!sink_adapter.consumed()) {
        // This implementation cannot handle the omit_empty_values for typed value
        // correctly and will always add a null.
        output.addNull();
      }
    }
  }

  sink.push_back('\n');
}

// A JSON array node in the format template tree used by OmitEmptyJsonFormatterImpl.
struct JsonFormatListNode;

// A value within the format template tree used by OmitEmptyJsonFormatterImpl. The value is one of:
// a pre-serialized constant scalar, a substitution command template, a nested object, or a nested
// array. Literal nulls are represented by absl::monostate and are dropped while the tree is built,
// so they are never stored in a node. The nested nodes are stored by value because they are backed
// by a vector, just like the command template, so this does not increase the size of the variant.
using JsonFormatValue =
    absl::variant<absl::monostate, // Literal null or unset: dropped at build time.
                  std::string,     // Pre-serialized constant JSON scalar.
                  std::vector<FormatterProviderPtr>, // Substitution command template.
                  JsonFormatMapNode,                 // Nested object.
                  JsonFormatListNode>;               // Nested array.

// A JSON object node in the format template tree used by OmitEmptyJsonFormatterImpl.
struct JsonFormatMapNode {
  // Fields are stored sorted by key to keep the output deterministic.
  std::vector<std::pair<std::string, JsonFormatValue>> fields_;
};

struct JsonFormatListNode {
  std::vector<JsonFormatValue> values_;
};

namespace {

using ProtoDict = Protobuf::Map<std::string, Protobuf::Value>;

absl::StatusOr<JsonFormatMapNode>
buildJsonFormatMapNode(const ProtoDict& fields, const std::vector<CommandParserPtr>& commands);

// Converts a single proto value from the JSON format configuration into a format template tree
// value. Substitution commands are parsed into providers at configuration load time.
absl::StatusOr<JsonFormatValue>
buildJsonFormatValue(const Protobuf::Value& value, const std::vector<CommandParserPtr>& commands) {
  switch (value.kind_case()) {
  case Protobuf::Value::kNumberValue: {
    std::string constant;
    JsonStringSerializer(constant).addNumber(value.number_value());
    return JsonFormatValue{std::move(constant)};
  }
  case Protobuf::Value::kBoolValue: {
    std::string constant;
    JsonStringSerializer(constant).addBool(value.bool_value());
    return JsonFormatValue{std::move(constant)};
  }
  case Protobuf::Value::kStringValue: {
    absl::string_view string_format = value.string_value();
    if (!absl::StrContains(string_format, '%')) {
      // Constant string: sanitize and quote it once at configuration load time.
      std::string constant;
      JsonStringSerializer(constant).addString(string_format);
      return JsonFormatValue{std::move(constant)};
    }
    // Substitution command template: parse it into providers.
    absl::StatusOr<std::vector<FormatterProviderPtr>> providers_or =
        SubstitutionFormatParser::parse(string_format, commands);
    RETURN_IF_NOT_OK_REF(providers_or.status());
    return JsonFormatValue{std::move(providers_or).value()};
  }
  case Protobuf::Value::kStructValue: {
    absl::StatusOr<JsonFormatMapNode> node_or =
        buildJsonFormatMapNode(value.struct_value().fields(), commands);
    RETURN_IF_NOT_OK_REF(node_or.status());
    return JsonFormatValue{std::move(node_or).value()};
  }
  case Protobuf::Value::kListValue: {
    JsonFormatListNode node;
    const auto& values = value.list_value().values();
    node.values_.reserve(values.size());
    for (const Protobuf::Value& element : values) {
      absl::StatusOr<JsonFormatValue> value_or = buildJsonFormatValue(element, commands);
      RETURN_IF_NOT_OK_REF(value_or.status());
      // Literal null elements are dropped so they are neither stored nor checked at format time.
      if (!absl::holds_alternative<absl::monostate>(value_or.value())) {
        node.values_.push_back(std::move(value_or).value());
      }
    }
    return JsonFormatValue{std::move(node)};
  }
  case Protobuf::Value::KIND_NOT_SET:
  case Protobuf::Value::kNullValue:
    break;
  }
  // A literal null or unset value is treated as empty and dropped while building the tree.
  return JsonFormatValue{absl::monostate{}};
}

absl::StatusOr<JsonFormatMapNode>
buildJsonFormatMapNode(const ProtoDict& fields, const std::vector<CommandParserPtr>& commands) {
  JsonFormatMapNode node;
  const auto sorted_fields = sortJsonStructFields(fields);
  node.fields_.reserve(sorted_fields.size());
  for (const auto& field : sorted_fields) {
    absl::StatusOr<JsonFormatValue> value_or = buildJsonFormatValue(field->second, commands);
    RETURN_IF_NOT_OK_REF(value_or.status());
    // Literal null fields are dropped so they are neither stored nor checked at format time.
    if (!absl::holds_alternative<absl::monostate>(value_or.value())) {
      node.fields_.emplace_back(field->first, std::move(value_or).value());
    }
  }
  return node;
}

bool serializeJsonFormatValue(const JsonFormatValue& value, const Context& context,
                              const StreamInfo::StreamInfo& info, JsonStringSerializer& serializer,
                              std::string& scratch);

// Serializes a map node into the output buffer. Returns true if the node produced any output. A
// node whose fields are all omitted produces no output and returns false so that its parent (or
// the root formatter) can drop it.
bool serializeJsonFormatMapNode(const JsonFormatMapNode& node, const Context& context,
                                const StreamInfo::StreamInfo& info,
                                JsonStringSerializer& serializer, std::string& scratch) {
  const size_t node_start = serializer.outputBuffer().size();
  serializer.addMapBeginDelimiter();
  bool object_is_empty = true;
  for (const auto& field : node.fields_) {
    const size_t field_start = serializer.outputBuffer().size();
    if (!object_is_empty) {
      serializer.addElementsDelimiter();
    }
    serializer.addString(field.first);
    serializer.addKeyValueDelimiter();
    if (!serializeJsonFormatValue(field.second, context, info, serializer, scratch)) {
      // The value was omitted; roll back the element delimiter, key and any partial output.
      serializer.outputBuffer().resize(field_start);
      continue;
    }
    object_is_empty = false;
  }
  if (object_is_empty) {
    // No fields were retained; drop the object so the caller can omit it.
    serializer.outputBuffer().resize(node_start);
    return false;
  }
  serializer.addMapEndDelimiter();
  return true;
}

// Serializes a list node into the output buffer. Null elements are skipped, but the array itself
// is always kept, so an array whose elements are all omitted is serialized as an empty array.
void serializeJsonFormatListNode(const JsonFormatListNode& node, const Context& context,
                                 const StreamInfo::StreamInfo& info,
                                 JsonStringSerializer& serializer, std::string& scratch) {
  serializer.addArrayBeginDelimiter();
  bool array_is_empty = true;
  for (const JsonFormatValue& element : node.values_) {
    const size_t element_start = serializer.outputBuffer().size();
    if (!array_is_empty) {
      serializer.addElementsDelimiter();
    }
    if (!serializeJsonFormatValue(element, context, info, serializer, scratch)) {
      serializer.outputBuffer().resize(element_start);
      continue;
    }
    array_is_empty = false;
  }
  serializer.addArrayEndDelimiter();
}

bool serializeJsonFormatValue(const JsonFormatValue& value, const Context& context,
                              const StreamInfo::StreamInfo& info, JsonStringSerializer& serializer,
                              std::string& scratch) {
  // A pre-serialized constant scalar is emitted directly.
  if (absl::holds_alternative<std::string>(value)) {
    serializer.addRawString(absl::get<std::string>(value));
    return true;
  }
  // A nested object; it is dropped if all of its fields are omitted.
  if (absl::holds_alternative<JsonFormatMapNode>(value)) {
    return serializeJsonFormatMapNode(absl::get<JsonFormatMapNode>(value), context, info,
                                      serializer, scratch);
  }
  // A nested array; it is always kept, even when empty.
  if (absl::holds_alternative<JsonFormatListNode>(value)) {
    serializeJsonFormatListNode(absl::get<JsonFormatListNode>(value), context, info, serializer,
                                scratch);
    return true;
  }

  // The only remaining alternative is a substitution command template; literal nulls are dropped
  // while building the tree and are therefore never stored.
  ASSERT(absl::holds_alternative<std::vector<FormatterProviderPtr>>(value));
  const auto& formatters = absl::get<std::vector<FormatterProviderPtr>>(value);
  ASSERT(!formatters.empty());
  if (formatters.size() == 1) {
    // Single provider: preserve the value type and omit the key when the value is null.
    ValueSink sink_adapter(serializer);
    formatters[0]->formatValueTo(sink_adapter, context, info);
    return sink_adapter.consumed();
  }

  // Multiple providers force a string output which is always kept, even if empty. Missing values
  // contribute an empty string because omit_empty_values is set.
  stringValueToLogLine(formatters, context, info, serializer, EMPTY_STRING, scratch);
  return true;
}

} // namespace

absl::StatusOr<std::unique_ptr<OmitEmptyJsonFormatterImpl>>
OmitEmptyJsonFormatterImpl::create(const Protobuf::Struct& struct_format,
                                   const CommandParsers& commands) {
  absl::StatusOr<JsonFormatMapNode> root_or =
      buildJsonFormatMapNode(struct_format.fields(), commands);
  RETURN_IF_NOT_OK_REF(root_or.status());
  return std::make_unique<OmitEmptyJsonFormatterImpl>(
      std::make_unique<JsonFormatMapNode>(std::move(root_or).value()));
}

OmitEmptyJsonFormatterImpl::OmitEmptyJsonFormatterImpl(std::unique_ptr<JsonFormatMapNode> root)
    : root_(std::move(root)) {}

OmitEmptyJsonFormatterImpl::~OmitEmptyJsonFormatterImpl() = default;

std::string OmitEmptyJsonFormatterImpl::format(const Context& context,
                                               const StreamInfo::StreamInfo& info) const {
  std::string log_line;
  log_line.reserve(2048);
  formatTo(log_line, context, info);
  return log_line;
}

void OmitEmptyJsonFormatterImpl::formatTo(std::string& sink, const Context& context,
                                          const StreamInfo::StreamInfo& info) const {
  std::string scratch; // Helper to hold the formatted value of a single provider.
  JsonStringSerializer serializer(sink);
  if (!serializeJsonFormatMapNode(*root_, context, info, serializer, scratch)) {
    // Every field was omitted; the root object is always emitted as an empty object.
    serializer.addMapBeginDelimiter();
    serializer.addMapEndDelimiter();
  }
  sink.push_back('\n');
}

} // namespace Formatter
} // namespace Envoy
