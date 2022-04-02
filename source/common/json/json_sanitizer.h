#pragma once

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {

/**
 * Sanitizes a string so it is suitable for JSON. The buffer is
 * used if any of the characters in str need to be escaped.
 *
 * @param buffer a string in which an escaped string can be written, if needed.
 *   It is not necessary for callers to clear the buffer first; it be cleared
 *   by this method if needed.
 * @param str the string to be translated
 * @return the translated string_view.
 */
absl::string_view sanitize(std::string& buffer, absl::string_view str);

} // namespace Json
} // namespace Envoy
