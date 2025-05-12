#include "source/common/formatter/substitution_format_string.h"

namespace Envoy {
namespace Formatter {

absl::StatusOr<std::vector<CommandParserPtr>> SubstitutionFormatStringUtils::parseFormatters(
    const FormattersConfig& formatters, Server::Configuration::GenericFactoryContext& context,
    std::vector<CommandParserPtr>&& commands_parsers) {
  std::vector<CommandParserPtr> commands = std::move(commands_parsers);
  for (const auto& formatter : formatters) {
    auto* factory = Envoy::Config::Utility::getFactory<CommandParserFactory>(formatter);
    if (!factory) {
      return absl::InvalidArgumentError(absl::StrCat("Formatter not found: ", formatter.name()));
    }
    auto typed_config = Envoy::Config::Utility::translateAnyToFactoryConfig(
        formatter.typed_config(), context.messageValidationVisitor(), *factory);
    auto parser = factory->createCommandParserFromProto(*typed_config, context);
    if (!parser) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to create command parser: ", formatter.name()));
    }
    commands.push_back(std::move(parser));
  }

  return commands;
}

absl::StatusOr<FormatterPtr> SubstitutionFormatStringUtils::fromProtoConfig(
    const envoy::config::core::v3::SubstitutionFormatString& config,
    Server::Configuration::GenericFactoryContext& context,
    std::vector<CommandParserPtr>&& command_parsers) {
  // Instantiate formatter extensions.
  auto commands = parseFormatters(config.formatters(), context, std::move(command_parsers));
  RETURN_IF_NOT_OK_REF(commands.status());

  switch (config.format_case()) {
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormat:
    return FormatterImpl::create(config.text_format(), config.omit_empty_values(), *commands);
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat:
    return createJsonFormatter(
        config.json_format(), true, config.omit_empty_values(),
        config.has_json_format_options() ? config.json_format_options().sort_properties() : false,
        *commands);
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormatSource: {
    auto data_source_or_error = Config::DataSource::read(config.text_format_source(), true,
                                                         context.serverFactoryContext().api());
    RETURN_IF_NOT_OK(data_source_or_error.status());
    return FormatterImpl::create(*data_source_or_error, config.omit_empty_values(), *commands);
  }
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::FORMAT_NOT_SET:
    PANIC_DUE_TO_PROTO_UNSET;
  }

  return nullptr;
}

FormatterPtr SubstitutionFormatStringUtils::createJsonFormatter(
    const ProtobufWkt::Struct& struct_format, bool preserve_types, bool omit_empty_values,
    bool sort_properties, const std::vector<CommandParserPtr>& commands) {

// TODO(alyssawilk, wbpcode) when deprecating logging_with_fast_json_formatter
// remove LegacyJsonFormatterImpl and StructFormatterBase
#ifndef ENVOY_DISABLE_EXCEPTIONS
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.logging_with_fast_json_formatter")) {
    return std::make_unique<LegacyJsonFormatterImpl>(struct_format, preserve_types,
                                                     omit_empty_values, sort_properties, commands);
  }
#else
  UNREFERENCED_PARAMETER(preserve_types);
  UNREFERENCED_PARAMETER(sort_properties);
#endif

  return std::make_unique<JsonFormatterImpl>(struct_format, omit_empty_values, commands);
}

} // namespace Formatter
} // namespace Envoy
