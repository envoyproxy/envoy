#pragma once

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {

/**
 * Sanitizes a string so it is suitable for JSON. The buffer is
 * used if any of the characters in str need to be escaped. Performance
 * is good if there are no characters requiring escaping or utf-8 decode.
 *
 * --------------------------------------------------------------------
 * Benchmark                          Time             CPU   Iterations
 * --------------------------------------------------------------------
 * BM_ProtoEncoderNoEscape         1445 ns         1444 ns       455727
 * BM_NlohmannNoEscape             9.79 ns         9.79 ns     71449511
 * BM_ProtoEncoderWithEscape       1521 ns         1521 ns       462697
 * BM_NlohmannWithEscape            215 ns          215 ns      3264218
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
