#include "common/formatter/substitution_format_string.h"

#include "envoy/api/api.h"

#include "common/config/datasource.h"
#include "common/config/utility.h"
#include "common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Formatter {

FormatterPtr
SubstitutionFormatStringUtils::createJsonFormatter(const ProtobufWkt::Struct& struct_format,
                                                   bool preserve_types, bool omit_empty_values) {
  return std::make_unique<JsonFormatterImpl>(struct_format, preserve_types, omit_empty_values);
}

FormatterPtr SubstitutionFormatStringUtils::fromProtoConfig(
    const envoy::config::core::v3::SubstitutionFormatString& config, Api::Api& api) {
  // Instantiate formatter extensions.
  std::vector<CommandParserPtr> commands;
  for (const auto& formatter : config.formatters()) {
    auto* factory =
        Envoy::Config::Utility::getFactoryByName<CommandParserFactory>(formatter.name());
    if (!factory) {
      throw EnvoyException(absl::StrCat("Formatter not found: ", formatter.name()));
    }
    auto parser = factory->createCommandParserFromProto(formatter.typed_config());
    commands.push_back(std::move(parser));
  }

  switch (config.format_case()) {
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormat:
    return std::make_unique<FormatterImpl>(config.text_format(), config.omit_empty_values(),
                                           commands);
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat: {
    return createJsonFormatter(config.json_format(), true, config.omit_empty_values());
  case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormatSource:
    return std::make_unique<FormatterImpl>(
        Config::DataSource::read(config.text_format_source(), true, api), false, commands);
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return nullptr;
}

} // namespace Formatter
} // namespace Envoy
