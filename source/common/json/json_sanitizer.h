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
// BM_ProtoEncoderNoEscape          1100 ns         1100 ns       636735
// BM_JsonSanitizerNoEscape         18.1 ns         18.1 ns     38671848
// BM_ProtoEncoderWithEscape        1376 ns         1375 ns       507137
// BM_JsonSanitizerWithEscape       94.5 ns         94.5 ns      7402468
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
   * @param buffer a string in which an escaped string can be written, if needed
   * @param str the string to be translated
   * @return the translated string_view.
   */
  absl::string_view sanitize(std::string& buffer, absl::string_view str) const;

private:
  // Character-indexed array of translation strings. If an entry is nullptr then
  // the character does not require substitution. This strategy is dependent on
  // the property of UTF-8 where all two-byte characters have the high-order bit
  // set for both bytes, and don't require escaping for JSON. Thus we can
  // consider each character in isolation for escaping.
  std::unique_ptr<std::string> char_escapes_[256];
};

} // namespace Json
} // namespace Envoy
