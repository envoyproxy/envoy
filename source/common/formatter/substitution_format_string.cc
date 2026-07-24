#include "source/common/formatter/substitution_format_string.h"

#include "source/common/runtime/runtime_features.h"

#include "absl/status/statusor.h"

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
    return createJsonFormatter(config.json_format(), config.omit_empty_values(), *commands);
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

absl::StatusOr<FormatterPtr>
SubstitutionFormatStringUtils::createJsonFormatter(const Protobuf::Struct& struct_format,
                                                   bool omit_empty_values,
                                                   const std::vector<CommandParserPtr>& commands) {
  // The pre-serialized JSON formatter cannot omit keys with null values because keys and
  // delimiters are serialized when loading the configuration. When omit_empty_values is set, use
  // the tree-structured formatter that can make the omission decision at format time instead.
  if (omit_empty_values && Runtime::runtimeFeatureEnabled(
                               "envoy.reloadable_features.json_formatter_omit_empty_values")) {
    return OmitEmptyJsonFormatterImpl::create(struct_format, commands);
  }
  return JsonFormatterImpl::create(struct_format, omit_empty_values, commands);
}

} // namespace Formatter
} // namespace Envoy
