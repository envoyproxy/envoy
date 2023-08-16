#pragma once

#include <string>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/substitution_format_string.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/server/factory_context.h"

#include "source/common/protobuf/protobuf.h"

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
  static FormatterPtr
  fromProtoConfig(const envoy::config::core::v3::SubstitutionFormatString& config,
                  Server::Configuration::CommonFactoryContext& context);

  /**
   * Generate a Json formatter object from proto::Struct config
   */
  static FormatterPtr createJsonFormatter(const ProtobufWkt::Struct& struct_format,
                                          bool preserve_types, bool omit_empty_values,
                                          bool sort_properties);
};

} // namespace Formatter
} // namespace Envoy
