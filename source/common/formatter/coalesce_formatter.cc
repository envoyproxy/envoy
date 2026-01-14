#include "source/common/formatter/coalesce_formatter.h"

#include "source/common/common/fmt.h"
#include "source/common/json/json_loader.h"

namespace Envoy {
namespace Formatter {

absl::StatusOr<FormatterProviderPtr> CoalesceFormatter::create(absl::string_view json_config,
                                                               absl::optional<size_t> max_length) {
  if (json_config.empty()) {
    return absl::InvalidArgumentError("COALESCE requires a JSON configuration parameter");
  }

  auto json_or_error = Json::Factory::loadFromString(std::string(json_config));
  if (!json_or_error.ok()) {
    return absl::InvalidArgumentError(fmt::format(
        "COALESCE: failed to parse JSON configuration: {}", json_or_error.status().message()));
  }

  const auto& json = *json_or_error.value();

  if (!json.hasObject("operators")) {
    return absl::InvalidArgumentError(
        "COALESCE: JSON configuration must contain 'operators' array");
  }

  auto operators_or_error = json.getObjectArray("operators");
  if (!operators_or_error.ok()) {
    return absl::InvalidArgumentError(fmt::format("COALESCE: 'operators' must be an array: {}",
                                                  operators_or_error.status().message()));
  }

  const auto& operators = operators_or_error.value();
  if (operators.empty()) {
    return absl::InvalidArgumentError("COALESCE: 'operators' array must not be empty");
  }

  std::vector<FormatterProviderPtr> formatters;
  formatters.reserve(operators.size());

  for (size_t i = 0; i < operators.size(); ++i) {
    const auto& entry = operators[i];
    auto formatter_or_error = parseOperatorEntry(*entry);
    if (!formatter_or_error.ok()) {
      return absl::InvalidArgumentError(
          fmt::format("COALESCE: failed to parse operator at index {}: {}", i,
                      formatter_or_error.status().message()));
    }
    formatters.push_back(std::move(formatter_or_error.value()));
  }

  return std::make_unique<CoalesceFormatter>(std::move(formatters), max_length);
}

absl::StatusOr<FormatterProviderPtr>
CoalesceFormatter::parseOperatorEntry(const Json::Object& entry) {
  // Check if this is a simple string command with command-only and no parameters.
  auto string_value = entry.asString();
  if (string_value.ok()) {
    return createFormatterForCommand(string_value.value(), "", absl::nullopt);
  }

  // Otherwise, it should be an object with "command" field.
  if (!entry.isObject()) {
    return absl::InvalidArgumentError(
        "operator entry must be either a string (command name) or an object with 'command' field");
  }

  auto command_or_error = entry.getString("command");
  if (!command_or_error.ok()) {
    return absl::InvalidArgumentError(fmt::format("operator object must have 'command' field: {}",
                                                  command_or_error.status().message()));
  }

  std::string param;
  if (entry.hasObject("param")) {
    auto param_or_error = entry.getString("param");
    if (!param_or_error.ok()) {
      return absl::InvalidArgumentError(
          fmt::format("'param' field must be a string: {}", param_or_error.status().message()));
    }
    param = param_or_error.value();
  }

  absl::optional<size_t> entry_max_length;
  if (entry.hasObject("max_length")) {
    auto max_length_or_error = entry.getInteger("max_length");
    if (!max_length_or_error.ok()) {
      return absl::InvalidArgumentError(fmt::format("'max_length' field must be an integer: {}",
                                                    max_length_or_error.status().message()));
    }
    if (max_length_or_error.value() <= 0) {
      return absl::InvalidArgumentError("'max_length' must be a positive integer");
    }
    entry_max_length = static_cast<size_t>(max_length_or_error.value());
  }

  return createFormatterForCommand(command_or_error.value(), param, entry_max_length);
}

absl::StatusOr<FormatterProviderPtr>
CoalesceFormatter::createFormatterForCommand(absl::string_view command, absl::string_view param,
                                             absl::optional<size_t> max_length) {
  // Try built-in command parsers to create the formatter.
  for (const auto& parser : BuiltInCommandParserFactoryHelper::commandParsers()) {
    auto formatter = parser->parse(command, param, max_length);
    if (formatter != nullptr) {
      return formatter;
    }
  }

  return absl::InvalidArgumentError(fmt::format("unknown command: '{}'", command));
}

absl::optional<std::string>
CoalesceFormatter::format(const Context& context, const StreamInfo::StreamInfo& stream_info) const {
  for (const auto& formatter : formatters_) {
    auto result = formatter->format(context, stream_info);
    if (result.has_value() && !result.value().empty()) {
      if (max_length_.has_value()) {
        SubstitutionFormatUtils::truncate(result.value(), max_length_.value());
      }
      return result;
    }
  }
  return absl::nullopt;
}

Protobuf::Value CoalesceFormatter::formatValue(const Context& context,
                                               const StreamInfo::StreamInfo& stream_info) const {
  for (const auto& formatter : formatters_) {
    auto result = formatter->formatValue(context, stream_info);
    // Check if this is a valid non-null value.
    if (result.kind_case() != Protobuf::Value::KIND_NOT_SET &&
        result.kind_case() != Protobuf::Value::kNullValue) {
      // For string values, also check if empty.
      if (result.kind_case() == Protobuf::Value::kStringValue) {
        if (!result.string_value().empty()) {
          if (max_length_.has_value() && result.string_value().size() > max_length_.value()) {
            result.set_string_value(result.string_value().substr(0, max_length_.value()));
          }
          return result;
        }
      } else {
        return result;
      }
    }
  }
  return SubstitutionFormatUtils::unspecifiedValue();
}

} // namespace Formatter
} // namespace Envoy
