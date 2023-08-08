#include "source/common/formatter/substitution_format_string.h"

#include "envoy/api/api.h"

#include "source/common/config/datasource.h"
#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/protobuf/message_validator_impl.h"

namespace Envoy {
namespace Formatter {

FormatterPtr
SubstitutionFormatStringUtils::createJsonFormatter(const ProtobufWkt::Struct& struct_format,
                                                   bool preserve_types, bool omit_empty_values) {
  std::vector<CommandParserPtr> commands;
  return std::make_unique<JsonFormatterImpl>(struct_format, preserve_types, omit_empty_values,
                                             commands);
}

FormatterPtr SubstitutionFormatStringUtils::fromProtoConfig(
    const envoy::config::core::v3::SubstitutionFormatString& config,
    Server::Configuration::CommonFactoryContext& context) {
  // Instantiate formatter extensions.
  std::vector<CommandParserPtr> commands;
  for (const auto& formatter : config.formatters()) {
    auto* factory = Envoy::Config::Utility::getFactory<CommandParserFactory>(formatter);
    if (!factory) {
      throw EnvoyException(absl::StrCat("Formatter not found: ", formatter.name()));
    }
    auto typed_config = Envoy::Config::Utility::translateAnyToFactoryConfig(
        formatter.typed_config(), context.messageValidationVisitor(), *factory);
    auto parser = factory->createCommandParserFromProto(*typed_config, context);
    if (!parser) {
      throw EnvoyException(absl::StrCat("Failed to create command parser: ", formatter.name()));
    }
    commands.push_back(std::move(parser));
  }

  switch (config.format_case()) {
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormat:
    return std::make_unique<FormatterImpl>(config.text_format(), config.omit_empty_values(),
                                           commands);
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat:
    return std::make_unique<JsonFormatterImpl>(config.json_format(), true,
                                               config.omit_empty_values(), commands);
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormatSource:
    return std::make_unique<FormatterImpl>(
        Config::DataSource::read(config.text_format_source(), true, context.api()), false,
        commands);
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::FORMAT_NOT_SET:
    PANIC_DUE_TO_PROTO_UNSET;
  }

  return nullptr;
}

} // namespace Formatter
} // namespace Envoy
