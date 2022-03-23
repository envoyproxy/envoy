#pragma once

#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {

// Hand-rolled JSON sanitizer that has exactly the same behavior as serializing
// through protobufs, but is more than 10x faster. From
// test/common/json/json_sanitizer_speed_test.cc:
//
// ---------------------------------------------------------------------------
// Benchmark                                 Time             CPU   Iterations
// ---------------------------------------------------------------------------
// BM_ProtoEncoderNoEscape                1089 ns         1089 ns       547657
// BM_JsonSanitizerNoEscape               17.6 ns         17.6 ns     39777516
// BM_StaticJsonSanitizerNoEscape         18.6 ns         18.6 ns     37789666
// BM_ProtoEncoderWithEscape              1308 ns         1308 ns       533386
// BM_JsonSanitizerWithEscape             96.0 ns         96.0 ns      7291029
// BM_StaticJsonSanitizerWithEscape       96.7 ns         96.7 ns      7236032
//
class JsonSanitizer {
public:
  static constexpr uint32_t Utf8_2ByteMask = 0b11100000;
  static constexpr uint32_t Utf8_2BytePattern = 0b11000000;

  static constexpr uint32_t Utf8_3ByteMask = 0b11110000;
  static constexpr uint32_t Utf8_3BytePattern = 0b11100000;

  static constexpr uint32_t Utf8_4ByteMask = 0b11111000;
  static constexpr uint32_t Utf8_4BytePattern = 0b11110000;

  static constexpr uint32_t Utf8_ContinueMask = 0b11000000;
  static constexpr uint32_t Utf8_ContinuePattern = 0b10000000;

  static constexpr uint32_t Utf8_Shift = 6;

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

  /**
   * Determines whether the input string is valid utf8. This is used for testing,
   * to avoid trying to do differentials against Protobuf json sanitization, which
   * produces noisy error messages and empty strings when presented with invalid
   * utf8.
   */
  static bool isValidUtf8(absl::string_view in);

  /** The Unicode code-point and the number of utf8-bytes consumed */
  using UnicodeSizePair = std::pair<uint32_t, uint32_t>;

  /**
   * Decodes a byte-stream of UTF8, returning the resulting unicode and the
   * number of bytes consumed as a pair.
   *
   * @param bytes The data with utf8 bytes.
   * @param size The number of bytes available in data
   * @return UnicodeSizePair(unicode, consumed) -- if the decode fails consumed will be 0.
   */
  static UnicodeSizePair decodeUtf8(const uint8_t* bytes, uint32_t size);

private:
  static constexpr uint32_t NumEscapes = 1 << 11; // 2^11=2048 codes possible in 2-byte utf8.

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

  static uint32_t char2uint32(char c) { return static_cast<uint32_t>(static_cast<uint8_t>(c)); }

  Escape char_escapes_[NumEscapes];
};

} // namespace Json
} // namespace Envoy
