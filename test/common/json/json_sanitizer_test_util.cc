#include "test/common/json/json_sanitizer_test_util.h"

#include <string>

#include "source/common/common/utility.h" // for IntervalSet.

#include "test/common/json/utf8.h"

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"

namespace Envoy {
namespace Json {
namespace TestUtil {

namespace {

constexpr uint32_t UnicodeEscapeLength = 6; // "\u1234"
constexpr absl::string_view UnicodeEscapePrefix = "\\u";

class InvalidUnicodeSet {
public:
  InvalidUnicodeSet() {
    // Workaround limitations in protobuf serialization by skipping certain
    // unicodes from differential fuzzing. See
    // https://github.com/protocolbuffers/protobuf/issues/9729

    // The invalid intervals are generated with the command:
    // bazel -c opt run test/common/json:gen_excluded_unicodes |& grep -v 'contains invalid UTF-8'

    // Avoid ranges where the protobuf serialization fails, returning an empty
    // string. Nlohmann also fails (throws exceptions) in this range but
    // sanitizer() will catch that an do simple escapes on the string.
    invalid_3byte_intervals_.insert(0xd800, 0xe000);

    // Avoid differential testing of Unicode ranges generated from 4-byte utf-8
    // where protobuf serialization generates two small Unicode values instead
    // of the correct one. This must be a protobuf serialization issue.
    invalid_4byte_intervals_.insert(0x1d173, 0x1d17b);
    invalid_4byte_intervals_.insert(0xe0001, 0xe0002);
    invalid_4byte_intervals_.insert(0xe0020, 0xe0080);
  }

  // Helper functions to see if the specified Unicode is in the 3-byte utf-8
  // exclusion set or the 4-byte utf-8 exclusion-set.
  bool isInvalid3Byte(uint32_t unicode) const { return invalid_3byte_intervals_.test(unicode); }
  bool isInvalid4Byte(uint32_t unicode) const { return invalid_4byte_intervals_.test(unicode); }

private:
  IntervalSetImpl<uint32_t> invalid_3byte_intervals_;
  IntervalSetImpl<uint32_t> invalid_4byte_intervals_;
};

const InvalidUnicodeSet& invalidUnicodeSet() { CONSTRUCT_ON_FIRST_USE(InvalidUnicodeSet); }

// Encodes a Unicode code point into its UTF-8 byte sequence.
// This function determines the number of bytes required for the given Unicode
// code point and constructs the corresponding UTF-8 string.
std::string encodeUtf8(uint32_t unicode) {
  std::string out;
  // Handle 1-byte UTF-8 characters (ASCII range).
  if (unicode < 0x80) {
    out.push_back(static_cast<char>(unicode));
  }
  // Handle 2-byte UTF-8 characters.
  else if (unicode < 0x800) {
    out.push_back(static_cast<char>(0xc0 | (unicode >> 6)));
    out.push_back(static_cast<char>(0x80 | (unicode & 0x3f)));
  }
  // Handle 3-byte UTF-8 characters.
  else if (unicode < 0x10000) {
    out.push_back(static_cast<char>(0xe0 | (unicode >> 12)));
    out.push_back(static_cast<char>(0x80 | ((unicode >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (unicode & 0x3f)));
  }
  // Handle 4-byte UTF-8 characters.
  else {
    out.push_back(static_cast<char>(0xf0 | (unicode >> 18)));
    out.push_back(static_cast<char>(0x80 | ((unicode >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((unicode >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (unicode & 0x3f)));
  }
  return out;
}

} // namespace

bool isProtoSerializableUtf8(absl::string_view in) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(in.data());
  uint32_t size = in.size();
  while (size != 0) {
    if ((*data & 0x80) == 0) {
      ++data;
      --size;
    } else {
      auto [unicode, consumed] = Utf8::decode(data, size);
      data += consumed;
      size -= consumed;

      switch (consumed) {
      case 2:
        break;
      case 3:
        if (invalidUnicodeSet().isInvalid3Byte(unicode)) {
          return false;
        }
        break;
      case 4:
        if (invalidUnicodeSet().isInvalid4Byte(unicode)) {
          return false;
        }
        break;
      default:
        return false;
      }
    }
  }
  return true;
}

// Decodes unicode hex escape \u1234 into 0x1234, returning success.
bool parseUnicode(absl::string_view str, uint32_t& hex_value) {
  if (absl::StartsWith(str, UnicodeEscapePrefix) && str.size() >= UnicodeEscapeLength) {
    // TODO(jmarantz): Github master,
    // https://github.com/abseil/abseil-cpp/blob/master/absl/strings/numbers.h
    // has absl::SimpleHexAtoi, enabling this impl to be
    //   return absl::SimpleHexAtoi(str.substr(2, 4), &hex_value);
    // In the meantime we must nul-terminate.
    std::string hex_str(str.data() + 2, 4);
    char* str_end;
    hex_value = strtoul(hex_str.c_str(), &str_end, 16);
    return str_end != nullptr && *str_end == '\0';
  }
  return false;
}

// Removes 'prefix_size' characters from the beginning of 'str'.
void removePrefix(absl::string_view& str, uint32_t prefix_size) {
  ASSERT(prefix_size > 0);
  ASSERT(prefix_size <= str.size());
  str = str.substr(prefix_size, str.size() - prefix_size);
}

// Compares a string that's possibly an escaped Unicode, e.g. \u1234, to
// one that is utf8-encoded.
bool compareUnicodeEscapeAgainstUtf8(absl::string_view& escaped, absl::string_view& utf8) {
  uint32_t escaped_unicode;
  if (parseUnicode(escaped, escaped_unicode)) {
    // If one side of the comparison is a Unicode escape,
    auto [unicode, consumed] = Utf8::decode(utf8);
    if (consumed != 0 && unicode == escaped_unicode) {
      removePrefix(utf8, consumed);
      removePrefix(escaped, UnicodeEscapeLength);
      return true;
    }
  }
  return false;
}

// Determines whether two strings differ only in whether they have
// literal utf-8 or escaped 3-byte Unicode. We do this equivalence
// comparison to enable differential fuzzing between sanitize() and
// protobuf JSON serialization. The protobuf implementation has made
// some hard-to-understand decisions about what to encode via Unicode
// escapes versus what to pass through as utf-8.
bool utf8Equivalent(absl::string_view a, absl::string_view b, std::string& diffs) {
  absl::string_view all_a = a;
  absl::string_view all_b = b;
  while (true) {
    if (a.empty() && b.empty()) {
      return true;
    } else if (a.empty() || b.empty()) {
      diffs = absl::StrFormat("`%s' and `%s` have different lengths", a, b);
      return false;
    } else if (a[0] == b[0]) {
      removePrefix(a, 1);
      removePrefix(b, 1);
    } else if (!compareUnicodeEscapeAgainstUtf8(a, b) && !compareUnicodeEscapeAgainstUtf8(b, a)) {
      diffs = absl::StrFormat("%s != %s, [%d]%c(0x02%x, \\%03o) != [%d] %c(0x02%x, \\%03o)", all_a,
                              all_b, a.data() - all_a.data(), a[0], a[0], a[0],
                              b.data() - all_b.data(), b[0], b[0], b[0]);
      return false;
    }
  }
}

// Decodes a JSON string with escape sequences into its raw form.
// This function processes the input `sanitized` string, which may contain
// Standard JSON escapes (e.g., newline, tab, double quote, backslash) and
// Unicode hexadecimal escapes (e.g., \\uFFFF). It converts these escapes
// back into their corresponding characters or UTF-8 sequences.
//
// If a '\' character is encountered, it indicates an escape sequence. The
// following character determines the type of escape.
// For '\u' escapes, it attempts to parse a 4-digit hexadecimal Unicode value.
//   - If the value is a high surrogate (0xD800-0xDBFF), it checks for a
//     subsequent low surrogate (0xDC00-0xDFFF) to form a surrogate pair,
//     which is then converted to a single 4-byte Unicode code point.
//   - If the Unicode value is less than 256, it's appended as a single byte.
//   - Otherwise (for wide Unicode characters or surrogate pairs), it's encoded
//     into its UTF-8 byte sequence using `encodeUtf8`.
// More info about unicode can be found at: https://datatracker.ietf.org/doc/html/rfc2781.
bool decodeEscapedJson(absl::string_view sanitized, std::string& decoded, std::string& errmsg) {
  // A map between a character to its escape character.
  static constexpr std::array<char, 256> escape_map = []() {
    std::array<char, 256> map = {};

    // By default maps every character to itself (Identity).
    // This handles cases like '\'' -> '\'' or '\\' -> '\\' automatically,
    // and ensures non-special chars (like 'c' in '\c') remain strictly 'c'.
    for (int i = 0; i < 256; ++i) {
      map[i] = static_cast<char>(i);
    }
    // Special handling for special escape characters.
    map['n'] = '\n';
    map['r'] = '\r';
    map['t'] = '\t';
    map['v'] = '\v';
    map['b'] = '\b';
    map['f'] = '\f';
    map['a'] = '\a';
    map['0'] = '\0';
    return map;
  }();

  // Iterate over the characters in sanitized, and decode the escaped characters.
  while (!sanitized.empty()) {
    if (sanitized[0] == '\\') {
      if (sanitized.size() < 2) {
        errmsg = "Unfinished escape";
        return false;
      }
      char c = sanitized[1];

      switch (c) {
      // For simple character escapes, append the decoded char and advance past the escape sequence.
      // Note that some control characters (e.g., '\0') are not allowed in JSON
      // (but \u0000 is allowed).
      case 'n':
      case 'r':
      case 't':
      case 'b':
      case 'f':
      case '\\':
      case '/':
      case '"':
        decoded.push_back(escape_map[c]);
        // Consume \c (2 characters).
        removePrefix(sanitized, 2);
        break;
      // \u cases need special handling.
      case 'u': {
        uint32_t unicode_hex = 0;
        if (sanitized.size() < UnicodeEscapeLength ||
            !parseUnicode(sanitized.substr(0, UnicodeEscapeLength), unicode_hex)) {
          errmsg = "Invalid unicode escape";
          return false;
        }
        removePrefix(sanitized, UnicodeEscapeLength);

        if (unicode_hex >= 0xD800 && unicode_hex <= 0xDBFF) {
          // High surrogate. Check for low surrogate.
          if (sanitized.size() >= UnicodeEscapeLength && absl::StartsWith(sanitized, "\\u")) {
            uint32_t low_surrogate;
            if (parseUnicode(sanitized.substr(0, UnicodeEscapeLength), low_surrogate) &&
                (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF)) {
              // Valid surrogate pair.
              unicode_hex = 0x10000 + ((unicode_hex - 0xD800) << 10) + (low_surrogate - 0xDC00);
              // Consume a Unicode hexadecimal escape sequence for the low surrogate.
              removePrefix(sanitized, UnicodeEscapeLength);
            }
          }
        }

        if (unicode_hex < 256) {
          decoded.push_back(static_cast<char>(unicode_hex));
        } else {
          decoded.append(encodeUtf8(unicode_hex));
        }
        // No further processing for 'u' case.
        break;
      }
      // Other escape characters are not allowed.
      default:
        errmsg = absl::StrFormat("Unknown escape: \\%c", c);
        return false;
      }
    } else {
      // Non-escaped character, just add to the decoded.
      decoded.push_back(sanitized[0]);
      removePrefix(sanitized, 1);
    }
  }
  return true;
}

} // namespace TestUtil
} // namespace Json
} // namespace Envoy
