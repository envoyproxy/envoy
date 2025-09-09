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
      Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>;

  /**
   * Parse list of formatter configurations to commands.
   */
  static absl::StatusOr<std::vector<CommandParserPtr>>
  parseFormatters(const FormattersConfig& formatters,
                  Server::Configuration::GenericFactoryContext& context,
                  std::vector<CommandParserPtr>&& commands_parsers = {});

  /**
   * Generate a formatter object from config SubstitutionFormatString.
   */
  static absl::StatusOr<FormatterPtr>
  fromProtoConfig(const envoy::config::core::v3::SubstitutionFormatString& config,
                  Server::Configuration::GenericFactoryContext& context,
                  std::vector<CommandParserPtr>&& command_parsers = {});
  /**
   * Generate a Json formatter object from proto::Struct config
   */
  static FormatterPtr createJsonFormatter(const Protobuf::Struct& struct_format,
                                          bool omit_empty_values,
                                          const std::vector<CommandParserPtr>& commands = {});
};

} // namespace Formatter
} // namespace Envoy
