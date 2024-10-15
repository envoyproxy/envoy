#include "source/common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Formatter {

const re2::RE2& SubstitutionFormatParser::commandWithArgsRegex() {
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

JsonFormatBuilder::FormatElements
JsonFormatBuilder::fromStruct(const ProtobufWkt::Struct& struct_format) {
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

void JsonFormatBuilder::formatValueToFormatElements(const ProtobufWkt::Value& value) {
  switch (value.kind_case()) {
  case ProtobufWkt::Value::KIND_NOT_SET:
  case ProtobufWkt::Value::kNullValue:
    serializer_.addNull();
    break;
  case ProtobufWkt::Value::kNumberValue:
    serializer_.addNumber(value.number_value());
    break;
  case ProtobufWkt::Value::kStringValue: {
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
  case ProtobufWkt::Value::kBoolValue:
    serializer_.addBool(value.bool_value());
    break;
  case ProtobufWkt::Value::kStructValue: {
    formatValueToFormatElements(value.struct_value().fields());
    break;
  case ProtobufWkt::Value::kListValue:
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

} // namespace Formatter
} // namespace Envoy
