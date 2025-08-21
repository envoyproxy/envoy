#pragma once

#include "envoy/formatter/http_formatter_context.h"
#include "envoy/formatter/substitution_formatter.h"

namespace Envoy {
namespace Formatter {

/**
 * Util class for HTTP access log format.
 */
class HttpSubstitutionFormatUtils {
public:
  static absl::StatusOr<FormatterPtr> defaultSubstitutionFormatter();
};

} // namespace Formatter
} // namespace Envoy
