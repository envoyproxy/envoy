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
 * Determines whether the input string can be serialized by protobufs. This is
 * used for testing, to avoid trying to do differentials against Protobuf json
 * sanitization, which produces noisy error messages and empty strings when
 * presented with some utf8 sequences that are valid according to spec.
 *
 * @param in the string to validate as utf-8.
 */
bool isProtoSerializableUtf8(absl::string_view in);

bool utf8Equivalent(absl::string_view a, absl::string_view b);

} // namespace Json
} // namespace Envoy
