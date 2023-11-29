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
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Formatter {

/**
 * Utilities for using envoy::config::core::v3::SubstitutionFormatString
 */
class SubstitutionFormatStringUtils {
public:
  /**
   * Generate a formatter object from config SubstitutionFormatString.
   */
  template <class FormatterContext = HttpFormatterContext>
  static FormatterBasePtr<FormatterContext>
  fromProtoConfig(const envoy::config::core::v3::SubstitutionFormatString& config,
                  Server::GenericFactoryContextImpl context) {
    // Instantiate formatter extensions.
    std::vector<CommandParserBasePtr<FormatterContext>> commands;
    for (const auto& formatter : config.formatters()) {
      auto* factory =
          Envoy::Config::Utility::getFactory<CommandParserFactoryBase<FormatterContext>>(formatter);
      if (!factory) {
        throwEnvoyExceptionOrPanic(absl::StrCat("Formatter not found: ", formatter.name()));
      }
      auto typed_config = Envoy::Config::Utility::translateAnyToFactoryConfig(
          formatter.typed_config(), context.messageValidationVisitor(), *factory);
      auto parser = factory->createCommandParserFromProto(*typed_config, context);
      if (!parser) {
        throwEnvoyExceptionOrPanic(
            absl::StrCat("Failed to create command parser: ", formatter.name()));
      }
      commands.push_back(std::move(parser));
    }

    switch (config.format_case()) {
    case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormat:
      return std::make_unique<FormatterBaseImpl<FormatterContext>>(
          config.text_format(), config.omit_empty_values(), commands);
    case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat:
      return std::make_unique<JsonFormatterBaseImpl<FormatterContext>>(
          config.json_format(), true, config.omit_empty_values(),
          config.has_json_format_options() ? config.json_format_options().sort_properties() : false,
          commands);
    case envoy::config::core::v3::SubstitutionFormatString::FormatCase::kTextFormatSource:
      return std::make_unique<FormatterBaseImpl<FormatterContext>>(
          Config::DataSource::read(config.text_format_source(), true,
                                   context.serverFactoryContext().api()),
          config.omit_empty_values(), commands);
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
                      bool omit_empty_values, bool sort_properties) {
    return std::make_unique<JsonFormatterBaseImpl<FormatterContext>>(
        struct_format, preserve_types, omit_empty_values, sort_properties);
  }
};

} // namespace Formatter
} // namespace Envoy
