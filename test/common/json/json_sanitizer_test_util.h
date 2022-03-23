#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {

/**
 * Strips double-quotes on first and last characters of str.
 *
 * @param str The string to strip double-quotes from.
 * @return The string without its surrounding double-quotes.
 */
absl::string_view stripDoubleQuotes(absl::string_view str);

} // namespace Json
} // namespace Envoy
