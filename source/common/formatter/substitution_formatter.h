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
class PlainStringFormatter : public FormatterProvider {
public:
  PlainStringFormatter(absl::string_view str) { str_.set_string_value(str); }

  // FormatterProvider
  absl::optional<std::string> formatWithContext(const Context&,
                                                const StreamInfo::StreamInfo&) const override {
    return str_.string_value();
  }
  ProtobufWkt::Value formatValueWithContext(const Context&,
                                            const StreamInfo::StreamInfo&) const override {
    return str_;
  }

private:
  ProtobufWkt::Value str_;
};

/**
 * FormatterProvider for numbers.
 */
class PlainNumberFormatter : public FormatterProvider {
public:
  PlainNumberFormatter(double num) { num_.set_number_value(num); }

  // FormatterProvider
  absl::optional<std::string> formatWithContext(const Context&,
                                                const StreamInfo::StreamInfo&) const override {
    std::string str = absl::StrFormat("%g", num_.number_value());
    return str;
  }
  ProtobufWkt::Value formatValueWithContext(const Context&,
                                            const StreamInfo::StreamInfo&) const override {
    return num_;
  }

private:
  ProtobufWkt::Value num_;
};

/**
 * Access log format parser.
 */
class SubstitutionFormatParser {
public:
  static absl::StatusOr<std::vector<FormatterProviderPtr>>
  parse(absl::string_view format, const std::vector<CommandParserPtr>& command_parsers = {});
};

inline constexpr absl::string_view DefaultUnspecifiedValueStringView = "-";

/**
 * Composite formatter implementation.
 */
class FormatterImpl : public Formatter {
public:
  using CommandParsers = std::vector<CommandParserPtr>;

  static absl::StatusOr<std::unique_ptr<FormatterImpl>>
  create(absl::string_view format, bool omit_empty_values = false,
         const CommandParsers& command_parsers = {});

  // Formatter
  std::string formatWithContext(const Context& context,
                                const StreamInfo::StreamInfo& stream_info) const override;

protected:
  FormatterImpl(absl::Status& creation_status, absl::string_view format,
                bool omit_empty_values = false, const CommandParsers& command_parsers = {})
      : omit_empty_values_(omit_empty_values) {
    auto providers_or_error = SubstitutionFormatParser::parse(format, command_parsers);
    SET_AND_RETURN_IF_NOT_OK(providers_or_error.status(), creation_status);
    providers_ = std::move(*providers_or_error);
  }

private:
  const bool omit_empty_values_;
  std::vector<FormatterProviderPtr> providers_;
};

class JsonFormatterImpl : public Formatter {
public:
  using CommandParsers = std::vector<CommandParserPtr>;
  using Formatter = FormatterProviderPtr;
  using Formatters = std::vector<Formatter>;

  JsonFormatterImpl(const ProtobufWkt::Struct& struct_format, bool omit_empty_values,
                    const CommandParsers& commands = {});

  // Formatter
  std::string formatWithContext(const Context& context,
                                const StreamInfo::StreamInfo& info) const override;

private:
  const bool omit_empty_values_;
  using ParsedFormatElement = absl::variant<std::string, Formatters>;
  std::vector<ParsedFormatElement> parsed_elements_;
};

// Helper classes for StructFormatter::StructFormatMapVisitor.
template <class... Ts> struct StructFormatMapVisitorHelper : Ts... {
  using Ts::operator()...;
};
template <class... Ts> StructFormatMapVisitorHelper(Ts...) -> StructFormatMapVisitorHelper<Ts...>;

#ifndef ENVOY_DISABLE_EXCEPTIONS
/**
 * An formatter for structured log formats, which returns a Struct proto that
 * can be converted easily into multiple formats.
 */
class StructFormatter {
public:
  using CommandParsers = std::vector<CommandParserPtr>;
  using PlainNumber = PlainNumberFormatter;
  using PlainString = PlainStringFormatter;

  StructFormatter(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                  bool omit_empty_values, const CommandParsers& commands = {})
      : omit_empty_values_(omit_empty_values), preserve_types_(preserve_types),
        struct_output_format_(FormatBuilder(commands).toFormatMapValue(format_mapping)) {}

  ProtobufWkt::Struct formatWithContext(const Context& context,
                                        const StreamInfo::StreamInfo& info) const {
    StructFormatMapVisitor visitor{
        [&](const std::vector<FormatterProviderPtr>& providers) {
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
      std::function<ProtobufWkt::Value(const std::vector<FormatterProviderPtr>&)>,
      std::function<ProtobufWkt::Value(const StructFormatter::StructFormatMapWrapper&)>,
      std::function<ProtobufWkt::Value(const StructFormatter::StructFormatListWrapper&)>>;

  // Methods for building the format map.
  class FormatBuilder {
  public:
    explicit FormatBuilder(const CommandParsers& commands) : commands_(commands) {}
    absl::StatusOr<std::vector<FormatterProviderPtr>>
    toFormatStringValue(const std::string& string_format) const {
      return SubstitutionFormatParser::parse(string_format, commands_);
    }
    std::vector<FormatterProviderPtr> toFormatNumberValue(double value) const {
      std::vector<FormatterProviderPtr> formatters;
      formatters.emplace_back(FormatterProviderPtr{new PlainNumber(value)});
      return formatters;
    }
    StructFormatMapWrapper toFormatMapValue(const ProtobufWkt::Struct& struct_format) const {
      auto output = std::make_unique<StructFormatMap>();
      for (const auto& pair : struct_format.fields()) {
        switch (pair.second.kind_case()) {
        case ProtobufWkt::Value::kStringValue:
          output->emplace(pair.first,
                          THROW_OR_RETURN_VALUE(toFormatStringValue(pair.second.string_value()),
                                                std::vector<FormatterProviderPtr>));
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
          output->emplace_back(THROW_OR_RETURN_VALUE(toFormatStringValue(value.string_value()),
                                                     std::vector<FormatterProviderPtr>));
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
  ProtobufWkt::Value providersCallback(const std::vector<FormatterProviderPtr>& providers,
                                       const Context& context,
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
      if (str.has_value()) {
        return ValueUtil::stringValue(*str);
      }
      // Returning an "empty string" (depending on omit_empty_values_) in case
      // of a formatting error.
      return ValueUtil::stringValue(omit_empty_values_ ? EMPTY_STRING
                                                       : DefaultUnspecifiedValueStringView);
    }
    // Multiple providers forces string output.
    std::string str;
    for (const auto& provider : providers) {
      const auto bit = provider->formatWithContext(context, stream_info);
      // Add the formatted value if there is one. Otherwise add a default value
      // of "-" if omit_empty_values_ is not set.
      if (bit.has_value()) {
        str += bit.value();
      } else if (!omit_empty_values_) {
        str += DefaultUnspecifiedValueStringView;
      }
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

  const StructFormatMapWrapper struct_output_format_;
};

using StructFormatterPtr = std::unique_ptr<StructFormatter>;

class LegacyJsonFormatterImpl : public Formatter {
public:
  using CommandParsers = std::vector<CommandParserPtr>;

  LegacyJsonFormatterImpl(const ProtobufWkt::Struct& format_mapping, bool preserve_types,
                          bool omit_empty_values, bool sort_properties,
                          const CommandParsers& commands = {})
      : struct_formatter_(format_mapping, preserve_types, omit_empty_values, commands),
        sort_properties_(sort_properties) {}

  // Formatter
  std::string formatWithContext(const Context& context,
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
  const StructFormatter struct_formatter_;
  const bool sort_properties_;
};

#endif // ENVOY_DISABLE_EXCEPTIONS

} // namespace Formatter
} // namespace Envoy
