#pragma once

#include <string>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/substitution_format_string.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/server/factory_context.h"

#include "source/common/config/datasource.h"
#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/runtime/runtime_features.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Formatter {

/**
 * Utilities for using envoy::config::core::v3::SubstitutionFormatString
 */
class SubstitutionFormatStringUtils {
public:
  using FormattersConfig =
      ProtobufWkt::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>;

  /**
   * Parse list of formatter configurations to commands.
   */
  template <class FormatterContext = HttpFormatterContext>
  static absl::StatusOr<std::vector<CommandParserBasePtr<FormatterContext>>>
  parseFormatters(const FormattersConfig& formatters,
                  Server::Configuration::GenericFactoryContext& context) {
    std::vector<CommandParserBasePtr<FormatterContext>> commands;
    for (const auto& formatter : formatters) {
      auto* factory =
          Envoy::Config::Utility::getFactory<CommandParserFactoryBase<FormatterContext>>(formatter);
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

  /**
   * Generate a formatter object from config SubstitutionFormatString.
   */
  template <class FormatterContext = HttpFormatterContext>
  static absl::StatusOr<FormatterBasePtr<FormatterContext>>
  fromProtoConfig(const envoy::config::core::v3::SubstitutionFormatString& config,
                  Server::Configuration::GenericFactoryContext& context) {
    // Instantiate formatter extensions.
    auto commands = parseFormatters<FormatterContext>(config.formatters(), context);
    RETURN_IF_NOT_OK_REF(commands.status());
    switch (config.format_case()) {
    case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormat:
      return FormatterBaseImpl<FormatterContext>::create(config.text_format(),
                                                         config.omit_empty_values(), *commands);
    case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat:
      return createJsonFormatter<FormatterContext>(
          config.json_format(), true, config.omit_empty_values(),
          config.has_json_format_options() ? config.json_format_options().sort_properties() : false,
          *commands);
    case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormatSource: {
      auto data_source_or_error = Config::DataSource::read(config.text_format_source(), true,
                                                           context.serverFactoryContext().api());
      RETURN_IF_NOT_OK(data_source_or_error.status());
      return FormatterBaseImpl<FormatterContext>::create(*data_source_or_error,
                                                         config.omit_empty_values(), *commands);
    }
    case envoy::config::core::v3::SubstitutionFormatString::FormatCase::FORMAT_NOT_SET:
      PANIC_DUE_TO_PROTO_UNSET;
    }

    return nullptr;
  }

  /**
   * Generate a Json formatter object from proto::Struct config
   */
  template <class FormatterContext = HttpFormatterContext>
  static FormatterBasePtr<FormatterContext>
  createJsonFormatter(const ProtobufWkt::Struct& struct_format, bool preserve_types,
                      bool omit_empty_values, bool sort_properties,
                      const std::vector<CommandParserBasePtr<FormatterContext>>& commands = {}) {

// TODO(alyssawilk, wbpcode) when deprecating logging_with_fast_json_formatter
// remove LegacyJsonFormatterBaseImpl and StructFormatterBase
#ifndef ENVOY_DISABLE_EXCEPTIONS
    if (!Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.logging_with_fast_json_formatter")) {
      return std::make_unique<LegacyJsonFormatterBaseImpl<FormatterContext>>(
          struct_format, preserve_types, omit_empty_values, sort_properties, commands);
    }
#else
    UNREFERENCED_PARAMETER(preserve_types);
    UNREFERENCED_PARAMETER(sort_properties);
#endif

    return std::make_unique<JsonFormatterImplBase<FormatterContext>>(struct_format,
                                                                     omit_empty_values, commands);
  }
};

} // namespace Formatter
} // namespace Envoy
