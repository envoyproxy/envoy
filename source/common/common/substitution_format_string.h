#pragma once

#include <string>
#include <unordered_map>

#include "envoy/access_log/access_log.h"
#include "envoy/config/core/v3/substitution_format_string.pb.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {

/**
 * Utilities for using envoy::config::core::v3::SubstitutionFormatString
 */
class SubstitutionFormatStringUtils {
public:
  /**
   * Generate a formatter object from config SubstitutionFormatString.
   */
  static AccessLog::FormatterPtr
  fromProtoConfig(const envoy::config::core::v3::SubstitutionFormatString& config);

  /**
   * Generate a Json formatter object from proto::Struct config
   */
  static AccessLog::FormatterPtr createJsonFormatter(const ProtobufWkt::Struct& struct_format,
                                                     bool preserve_types);
};

} // namespace Envoy
