#include "source/common/json/json_sanitizer.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"
#include "source/common/json/json_internal.h"

#include "absl/strings/str_format.h"
#include "utf8_validity.h"

namespace Envoy {
namespace Json {

// clang-format off
// SPELLCHECKER(off)
//
// Performance benchmarks show this is slightly faster as an array of uint32_t
// rather than an array of bool.
static constexpr uint32_t needs_slow_sanitizer[256] = {
  // Control-characters 0-31 all require escapes.
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

  // Pass through printable characters starting with space. Double-quote and
  // backslash require an escape.
  0, 0, 1 /* " */, 0, 0, 0, 0, 0,         //  !"#$%&'
  0, 0, 0, 0, 0, 0, 0, 0,                 // ()*+,-.7
  0, 0, 0, 0, 0, 0, 0, 0,                 // 01234567
  0, 0, 0, 0, 0, 0, 0, 0,                 // 89:;<=>?
  0, 0, 0, 0, 0, 0, 0, 0,                 // @ABCDEFG
  0, 0, 0, 0, 0, 0, 0, 0,                 // HIJKLMNO
  0, 0, 0, 0, 0, 0, 0, 0,                 // PQRSTUVW
  0, 0, 0, 0, 1 /* backslash */, 0, 0, 0, // XYZ[\]^_
  0, 0, 0, 0, 0, 0, 0, 0,                 // `abcdefg
  0, 0, 0, 0, 0, 0, 0, 0,                 // hijklmno
  0, 0, 0, 0, 0, 0, 0, 0,                 // pqrstuvw
  0, 0, 0, 0, 0, 0, 0, 1,                 // xyz{|}~\177

  // 0x80-0xff, all of which require calling the slow sanitizer.
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};
// SPELLCHECKER(on)
// clang-format on

absl::string_view sanitize(std::string& buffer, absl::string_view str) {
  // Fast-path to see whether any escapes or utf-encoding are needed. If str has
  // only unescaped ascii characters, we can simply return it.
  //
  // Benchmarks show it's faster to just rip through the string with no
  // conditionals, so we only check the arithmetically ORed condition after the
  // loop. This avoids branches and allows simpler loop unrolling by the
  // compiler.
  static_assert(ARRAY_SIZE(needs_slow_sanitizer) == 256);
  uint32_t need_slow = 0;
  for (char c : str) {
    // We need to escape control characters, characters >= 127, and double-quote
    // and backslash.
    need_slow |= needs_slow_sanitizer[static_cast<uint8_t>(c)];
  }
  if (need_slow == 0) {
    return str; // Fast path, should be executed most of the time.
  }

  // Slow path.

  // If the input is valid utf-8, we can serialize it with Nlohmann.
  if (utf8_range::IsStructurallyValid(str)) {
    buffer = Nlohmann::Factory::serialize(str);
    return stripDoubleQuotes(buffer);
  }

  // If the input contains any invalid utf-8, escape all non-ascii characters
  // as octal escape sequences.
  buffer.clear();
  for (char c : str) {
    if (needs_slow_sanitizer[static_cast<uint8_t>(c)]) {
      buffer.append(absl::StrFormat("\\%03o", c));
    } else {
      buffer.append(1, c);
    }
  }
  return buffer;
}

absl::string_view stripDoubleQuotes(absl::string_view str) {
  if (str.size() >= 2 && str[0] == '"' && str[str.size() - 1] == '"') {
    str = str.substr(1, str.size() - 2);
  } else {
    ASSERT(false,
           absl::StrCat("stripDoubleQuotes called on a str that lacks double-quotes: ", str));
  }
  return str;
}

} // namespace Json
} // namespace Envoy
