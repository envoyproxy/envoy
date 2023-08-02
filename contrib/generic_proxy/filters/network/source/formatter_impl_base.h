#pragma once

#include <regex>

#include "envoy/config/core/v3/substitution_format_string.pb.h"

#include "source/common/config/datasource.h"
#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_formatter.h"

#include "absl/strings/str_format.h"
#include "contrib/generic_proxy/filters/network/source/formatter_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

inline constexpr absl::string_view DefaultUnspecifiedValueString = "-";

/**
 * FormatterProvider for string literals. It ignores headers and stream info and returns string by
 * which it was initialized.
 */
template <class FormatterContext>
class PlainStringFormatterProvider : public FormatterProvider<FormatterContext> {
public:
  PlainStringFormatterProvider(absl::string_view str) { str_.set_string_value(str); }

  // FormatterProvider
  absl::optional<std::string> format(const FormatterContext&,
                                     const StreamInfo::StreamInfo&) const override {
    return str_.string_value();
  }
  ProtobufWkt::Value formatValue(const FormatterContext&,
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
class PlainNumberFormatterProvider : public FormatterProvider<FormatterContext> {
public:
  PlainNumberFormatterProvider(double num) { num_.set_number_value(num); }

  // FormatterProvider
  absl::optional<std::string> format(const FormatterContext&,
                                     const StreamInfo::StreamInfo&) const override {
    std::string str = absl::StrFormat("%g", num_.number_value());
    return str;
  }
  ProtobufWkt::Value formatValue(const FormatterContext&,
                                 const StreamInfo::StreamInfo&) const override {
    return num_;
  }

private:
  ProtobufWkt::Value num_;
};

template <class FormatterContext>
class StringValueFormatterProvider : public FormatterProvider<FormatterContext> {
public:
  using ValueExtractor = std::function<absl::optional<std::string>(const FormatterContext&,
                                                                   const StreamInfo::StreamInfo&)>;

  StringValueFormatterProvider(ValueExtractor f, absl::optional<size_t> max_length = absl::nullopt)
      : value_extractor_(f), max_length_(max_length) {}

  // GenericProxyFormatterProvider
  absl::optional<std::string> format(const FormatterContext& context,
                                     const StreamInfo::StreamInfo& stream_info) const override {
    auto optional_str = value_extractor_(context, stream_info);
    if (!optional_str) {
      return absl::nullopt;
    }
    if (max_length_.has_value()) {
      if (optional_str->length() > max_length_.value()) {
        optional_str->resize(max_length_.value());
      }
    }
    return optional_str;
  }
  ProtobufWkt::Value formatValue(const FormatterContext& context,
                                 const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::optionalStringValue(format(context, stream_info));
  }

private:
  ValueExtractor value_extractor_;
  absl::optional<size_t> max_length_;
};

template <class FormatterContext>
class StreamInfoFormatterProvider : public FormatterProvider<FormatterContext> {
public:
  StreamInfoFormatterProvider(const std::string& command, const std::string& sub_command,
                              absl::optional<size_t> max_length) {
    const auto& extractors = Envoy::Formatter::StreamInfoFormatter::getKnownFieldExtractors();

    auto it = extractors.find(command);

    if (it == extractors.end()) {
      throw EnvoyException(fmt::format("Not supported field in StreamInfo: {}", command));
    }

    // Check flags for the command.
    Envoy::Formatter::CommandSyntaxChecker::verifySyntax((*it).second.first, command, sub_command,
                                                         max_length);

    // Create a pointer to the formatter by calling a function
    // associated with formatter's name.
    field_extractor_ = (*it).second.second(sub_command, max_length);
  }

  // FormatterProvider
  absl::optional<std::string> format(const FormatterContext&,
                                     const StreamInfo::StreamInfo& info) const override {
    return field_extractor_->extract(info);
  }
  ProtobufWkt::Value formatValue(const FormatterContext&,
                                 const StreamInfo::StreamInfo& info) const override {
    return field_extractor_->extractValue(info);
  }

private:
  Envoy::Formatter::StreamInfoFormatter::FieldExtractorPtr field_extractor_;
};

template <class FormatterContext> class SubstitutionFormatParser {
public:
  static std::vector<FormatterProviderPtr<FormatterContext>>
  parse(absl::string_view format, const CommandParsers<FormatterContext>& command_parsers) {
    std::string current_token;
    std::vector<FormatterProviderPtr<FormatterContext>> formatters;

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
  // Non-capturing group specifying optional :LENGTH -------------------------------------
  //                                                                                      |
  // Non-capturing group specifying optional (SUBCOMMAND)------------------               |
  //                                                                       |              |
  // Non-capturing group specifying mandatory COMMAND                      |              |
  //  which uses only A-Z, 0-9 and _ characters     -----                  |              |
  //  Group is used only to specify allowed characters.  |                 |              |
  //                                                     |                 |              |
  //                                                     |                 |              |
  //                                             _________________  _______________  _____________
  //                                             |               |  |             |  |           |
  const std::regex command_w_args_regex(R"EOF(^%((?:[A-Z]|[0-9]|_)+)(?:\(([^\)]*)\))?(?::([0-9]+))?%)EOF");
  //                                            |__________________|     |______|        |______|
  //                                                     |                   |              |
  // Capturing group specifying COMMAND -----------------                    |              |
  // The index of this group is 1.                                           |              |
  //                                                                         |              |
  // Capturing group for SUBCOMMAND. If present, it will --------------------               |
  // contain SUBCOMMAND without "(" and ")". The index                                      |
  // of SUBCOMMAND group is 2.                                                              |
  //                                                                                        |
  // Capturing group for LENGTH. If present, it will ----------------------------------------
  // contain just number without ":". The index of
  // LENGTH group is 3.
    // clang-format on

    for (size_t pos = 0; pos < format.size(); ++pos) {
      if (format[pos] != '%') {
        current_token += format[pos];
        continue;
      }

      // escape '%%'
      if (format.size() > pos + 1) {
        if (format[pos + 1] == '%') {
          current_token += '%';
          pos++;
          continue;
        }
      }

      if (!current_token.empty()) {
        formatters.emplace_back(FormatterProviderPtr<FormatterContext>{
            new PlainStringFormatterProvider<FormatterContext>(current_token)});
        current_token = "";
      }

      std::smatch m;
      const std::string search_space = std::string(format.substr(pos));
      if (!std::regex_search(search_space, m, command_w_args_regex)) {
        throw EnvoyException(
            fmt::format("Incorrect configuration: {}. Couldn't find valid command at position {}",
                        format, pos));
      }

      const std::string match = m.str(0);
      // command is at at index 1.
      const std::string command = m.str(1);
      // subcommand is at index 2.
      const std::string subcommand = m.str(2);
      // optional length is at index 3. If present, validate that it is valid integer.
      absl::optional<size_t> max_length;
      if (m.str(3).length() != 0) {
        size_t length_value;
        if (!absl::SimpleAtoi(m.str(3), &length_value)) {
          throw EnvoyException(absl::StrCat("Length must be an integer, given: ", m.str(3)));
        }
        max_length = length_value;
      }
      std::vector<std::string> path;

      const size_t command_end_position = pos + m.str(0).length() - 1;

      bool added = false;
      for (const auto& cmd : command_parsers) {
        auto formatter = cmd->parse(command, subcommand, max_length);
        if (formatter) {
          formatters.push_back(std::move(formatter));
          added = true;
          break;
        }
      }

      if (!added) {
        formatters.emplace_back(FormatterProviderPtr<FormatterContext>{
            new StreamInfoFormatterProvider<FormatterContext>(command, subcommand, max_length)});
      }

      pos = command_end_position;
    }

    if (!current_token.empty() || format.empty()) {
      // Create a PlainStringFormatterProvider with the final string literal. If the format string
      // was empty, this creates a PlainStringFormatterProvider with an empty string.
      formatters.emplace_back(FormatterProviderPtr<FormatterContext>{
          new PlainStringFormatterProvider<FormatterContext>(current_token)});
    }

    return formatters;
  }
};

template <class FormatterContext> class FormatterImpl : public Formatter<FormatterContext> {
public:
  FormatterImpl(absl::string_view format, bool omit_empty_values,
                const CommandParsers<FormatterContext>& command_parsers)
      : empty_value_string_(omit_empty_values ? absl::string_view{}
                                              : DefaultUnspecifiedValueString) {
    providers_ = SubstitutionFormatParser<FormatterContext>::parse(format, command_parsers);
  }

  // Formatter
  std::string format(const FormatterContext& context,
                     const StreamInfo::StreamInfo& info) const override {
    std::string log_line;
    log_line.reserve(256);

    for (const FormatterProviderPtr<FormatterContext>& provider : providers_) {
      const auto bit = provider->format(context, info);
      log_line += bit.value_or(empty_value_string_);
    }

    return log_line;
  }

private:
  const std::string empty_value_string_;
  std::vector<FormatterProviderPtr<FormatterContext>> providers_;
};

// Helper classes for StructFormatter::StructFormatMapVisitor.
template <class... Ts> struct StructFormatMapVisitorHelper : Ts... { using Ts::operator()...; };
template <class... Ts> StructFormatMapVisitorHelper(Ts...) -> StructFormatMapVisitorHelper<Ts...>;

/**
 * An formatter for structured log formats, which returns a Struct proto that
 * can be converted easily into multiple formats.
 */
template <class FormatterContext> class StructFormatter {
public:
  StructFormatter(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                  bool omit_empty_values, const CommandParsers<FormatterContext>& commands)
      : omit_empty_values_(omit_empty_values), preserve_types_(preserve_types),
        empty_value_(omit_empty_values_ ? std::string() : DefaultUnspecifiedValueString),
        struct_output_format_(FormatBuilder(commands).toFormatMapValue(format_mapping)) {}

  ProtobufWkt::Struct format(const FormatterContext& context,
                             const StreamInfo::StreamInfo& info) const {
    StructFormatMapVisitor visitor{
        [&](const std::vector<FormatterProviderPtr<FormatterContext>>& providers) {
          return providersCallback(providers, context, info);
        },
        [&, this](const StructFormatter::StructFormatMapWrapper& format_map) {
          return structFormatMapCallback(format_map, visitor);
        },
        [&, this](const StructFormatter::StructFormatListWrapper& format_list) {
          return structFormatListCallback(format_list, visitor);
        },
    };
    return structFormatMapCallback(struct_output_format_, visitor).struct_value();
  }

private:
  struct StructFormatMapWrapper;
  struct StructFormatListWrapper;
  using StructFormatValue =
      absl::variant<const std::vector<FormatterProviderPtr<FormatterContext>>,
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
          const std::vector<FormatterProviderPtr<FormatterContext>>&)>,
      const std::function<ProtobufWkt::Value(const StructFormatter::StructFormatMapWrapper&)>,
      const std::function<ProtobufWkt::Value(const StructFormatter::StructFormatListWrapper&)>>;

  // Methods for building the format map.
  class FormatBuilder {
  public:
    explicit FormatBuilder(const CommandParsers<FormatterContext>& commands)
        : commands_(commands) {}
    explicit FormatBuilder() : commands_(absl::nullopt) {}
    std::vector<FormatterProviderPtr<FormatterContext>>
    toFormatStringValue(const std::string& string_format) const {
      return SubstitutionFormatParser<FormatterContext>::parse(string_format, commands_);
    }
    std::vector<FormatterProviderPtr<FormatterContext>> toFormatNumberValue(double value) const {
      std::vector<FormatterProviderPtr<FormatterContext>> formatters;
      formatters.emplace_back(FormatterProviderPtr<FormatterContext>{
          new PlainNumberFormatterProvider<FormatterContext>(value)});
      return formatters;
    }
    StructFormatMapWrapper toFormatMapValue(const ProtobufWkt::Struct& struct_format) const {
      auto output = std::make_unique<StructFormatMap>();
      for (const auto& pair : struct_format.fields()) {
        switch (pair.second.kind_case()) {
        case ProtobufWkt::Value::kStringValue:
          output->emplace(pair.first, toFormatStringValue(pair.second.string_value()));
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
          output->emplace_back(toFormatStringValue(value.string_value()));
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
    const CommandParsers<FormatterContext>& commands_;
  };

  // Methods for doing the actual formatting.
  ProtobufWkt::Value
  providersCallback(const std::vector<FormatterProviderPtr<FormatterContext>>& providers,
                    const FormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const {
    ASSERT(!providers.empty());
    if (providers.size() == 1) {
      const auto& provider = providers.front();
      if (preserve_types_) {
        return provider->formatValue(context, stream_info);
      }

      if (omit_empty_values_) {
        return ValueUtil::optionalStringValue(provider->format(context, stream_info));
      }

      const auto str = provider->format(context, stream_info);
      return ValueUtil::stringValue(str.value_or(empty_value_));
    }
    // Multiple providers forces string output.
    std::string str;
    for (const auto& provider : providers) {
      const auto bit = provider->format(context, stream_info);
      str += bit.value_or(empty_value_);
    }
    return ValueUtil::stringValue(str);
  }
  ProtobufWkt::Value
  structFormatMapCallback(const StructFormatter::StructFormatMapWrapper& format_map,
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
  structFormatListCallback(const StructFormatter::StructFormatListWrapper& format_list,
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
using StructFormatterPtr = std::unique_ptr<StructFormatter<FormatterContext>>;

template <class FormatterContext> class JsonFormatterImpl : public Formatter<FormatterContext> {
public:
  JsonFormatterImpl(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                    bool omit_empty_values, const CommandParsers<FormatterContext>& commands)
      : struct_formatter_(format_mapping, preserve_types, omit_empty_values, commands) {}

  // Formatter::format
  std::string format(const FormatterContext& context,
                     const StreamInfo::StreamInfo& info) const override {
    const ProtobufWkt::Struct output_struct = struct_formatter_.format(context, info);

#ifdef ENVOY_ENABLE_YAML
    const std::string log_line =
        MessageUtil::getJsonStringFromMessageOrError(output_struct, false, true);
#else
    IS_ENVOY_BUG("Json support compiled out");
    const std::string log_line = "";
#endif
    return absl::StrCat(log_line, "\n");
  }

private:
  const StructFormatter<FormatterContext> struct_formatter_;
};

class SubstitutionFormatStringUtils {
public:
  /**
   * Generate a formatter object from config SubstitutionFormatString.
   */
  template <class FormatterContext>
  static FormatterPtr<FormatterContext>
  fromProtoConfig(const envoy::config::core::v3::SubstitutionFormatString& config,
                  Server::Configuration::CommonFactoryContext& context) {

    auto commands_ref = std::ref(BuiltInCommandParsers<FormatterContext>::commandParsers());

    // Try instantiate formatter extensions if exist.
    CommandParsers<FormatterContext> final_commands;
    if (!config.formatters().empty()) {
      // If there are configured formatters, then copy the built-in commands into the
      // final map first.
      final_commands = BuiltInCommandParsers<FormatterContext>::commandParsers();
      commands_ref = std::ref(final_commands);
    }

    for (const auto& formatter : config.formatters()) {
      auto* factory =
          Envoy::Config::Utility::getFactory<CommandParserFactory<FormatterContext>>(formatter);
      if (!factory) {
        throw EnvoyException(absl::StrCat("Formatter not found: ", formatter.name()));
      }
      auto typed_config = Envoy::Config::Utility::translateAnyToFactoryConfig(
          formatter.typed_config(), context.messageValidationVisitor(), *factory);
      auto parser = factory->createCommandParserFromProto(*typed_config, context);
      if (!parser) {
        throw EnvoyException(absl::StrCat("Failed to create command parser: ", formatter.name()));
      }
      // The configured command parsers may override the built-in command parsers.
      final_commands.push_back(std::move(parser));
    }

    switch (config.format_case()) {
    case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormat:
      return std::make_unique<FormatterImpl<FormatterContext>>(
          config.text_format(), config.omit_empty_values(), commands_ref.get());
    case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat:
      return std::make_unique<JsonFormatterImpl<FormatterContext>>(
          config.json_format(), true, config.omit_empty_values(), commands_ref.get());
    case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormatSource:
      return std::make_unique<FormatterImpl<FormatterContext>>(
          Config::DataSource::read(config.text_format_source(), true, context.api()), false,
          commands_ref.get());
    case envoy::config::core::v3::SubstitutionFormatString::FormatCase::FORMAT_NOT_SET:
      PANIC_DUE_TO_PROTO_UNSET;
    }

    return nullptr;
  }

  /**
   * Generate a Json formatter object from proto::Struct config
   */
  template <class FormatterContext>
  static FormatterPtr<FormatterContext>
  createJsonFormatter(const ProtobufWkt::Struct& struct_format, bool preserve_types,
                      bool omit_empty_values) {
    return std::make_unique<JsonFormatterImpl<FormatterContext>>(
        struct_format, preserve_types, omit_empty_values,
        BuiltInCommandParsers<FormatterContext>::commandParsers());
  }
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
