#pragma once

#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {

// Hand-rolled JSON sanitizer that has exactly the same behavior as serializing
// through protobufs, but is more than 10x faster. From
// test/common/json/json_sanitizer_speed_test.cc:
//
// ---------------------------------------------------------------------
// Benchmark                           Time             CPU   Iterations
// ---------------------------------------------------------------------
// BM_ProtoEncoderNoEscape          1102 ns         1102 ns       542926
// BM_JsonSanitizerNoEscape         12.0 ns         12.0 ns     57347682
// BM_ProtoEncoderWithEscape        1377 ns         1377 ns       507363
// BM_JsonSanitizerWithEscape       77.3 ns         77.3 ns      8729626
//
class JsonSanitizer {
public:
  // Constructing the sanitizer fills in a table with all escape-sequences,
  // indexed by character. To make this perform well, you should instantiate the
  // sanitizer in a context that lives across a large number of sanitizations.
  JsonSanitizer();

  /**
   * Sanitizes a string so it is suitable for JSON. The buffer is
   * used if any of the characters in str need to be escaped.
   *
   * @param buffer a string in which an escaped string can be written, if needed. It
   *   is not necessary for callers to clear the buffer first; it be cleared
   *   by this method if the input needs to be escaped.
   * @param str the string to be translated
   * @return the translated string_view.
   */
  absl::string_view sanitize(std::string& buffer, absl::string_view str) const;

private:
  // Character-indexed array of translation strings. If an entry is nullptr then
  // the character does not require substitution. This strategy is dependent on
  // the property of UTF-8 where all two-byte characters have the high-order bit
  // set for both bytes, and don't require escaping for JSON. Thus we can
  // consider each character in isolation for escaping. Reference:
  // https://en.wikipedia.org/wiki/UTF-8.
  struct Escape {
    uint8_t size_{0};
    char chars_[7]; // No need to initialize char data, as we are not null-terminating.
  };
  Escape char_escapes_[256];
};

} // namespace Json
} // namespace Envoy
