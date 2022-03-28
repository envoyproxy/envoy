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

/**
 * Determines whether the input string is valid utf8. This is used for testing,
 * to avoid trying to do differentials against Protobuf json sanitization, which
 * produces noisy error messages and empty strings when presented with invalid
 * utf8.
 *
 * @param in the string to validate as utf-8.
 * @param exclude_protobuf_exceptions whether invalidate utf8 strings that decode
 *        to unicode values that are rejected by the protobuf serializer.
 */
bool isValidUtf8(absl::string_view in, bool exclude_protobuf_exceptions);

} // namespace Json
} // namespace Envoy
