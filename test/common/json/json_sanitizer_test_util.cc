#include "test/common/json/json_sanitizer_test_util.h"

#include <string>

#include "source/common/common/utility.h" // for IntervalSet.

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"

namespace Envoy {
namespace Json {
namespace TestUtil {

absl::string_view stripDoubleQuotes(absl::string_view str) {
  if (str.size() >= 2 && str[0] == '"' && str[str.size() - 1] == '"') {
    return str.substr(1, str.size() - 2);
  }
  return str;
}

namespace {

class InvalidUnicodeSet {
public:
  InvalidUnicodeSet() {
    // Generated with
    //   bazel build -c opt test/common/json:json_sanitizer_test
    //   GENERATE_INVALID_UTF8_RANGES=1
    //     ./bazel-bin/test/common/json/json_sanitizer_test |&
    //     grep -v 'contains invalid UTF-8'

    // Avoid ranges where the protobuf serialization fails, returning an empty
    // string. Nlohmann also fails (throws exceptions) in this range but
    // sanitizer() will catch that an do simple escapes on the string.
    invalid_3byte_intervals_.insert(0xd800, 0xe000);

    // Avoid differential testing of unicode ranges generated from 4-byte utf-8
    // where protobuf serialization generates two small unicode values instead
    // of the correct one. This must be a protobuf serialization issue.
    invalid_4byte_intervals_.insert(0x1d173, 0x1d17b);
    invalid_4byte_intervals_.insert(0xe0001, 0xe0002);
    invalid_4byte_intervals_.insert(0xe0020, 0xe0080);
  }

  // Helper functions to see if the specified unicode is in the 3-byte utf-8
  // exclusion set or the 4-byte utf-8 exclusion-set.
  bool isInvalid3Byte(uint32_t unicode) const { return invalid_3byte_intervals_.test(unicode); }
  bool isInvalid4Byte(uint32_t unicode) const { return invalid_4byte_intervals_.test(unicode); }

private:
  IntervalSetImpl<uint32_t> invalid_3byte_intervals_;
  IntervalSetImpl<uint32_t> invalid_4byte_intervals_;
};

const InvalidUnicodeSet& invalidUnicodeSet() { CONSTRUCT_ON_FIRST_USE(InvalidUnicodeSet); }

} // namespace

bool isProtoSerializableUtf8(absl::string_view in) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(in.data());
  uint32_t size = in.size();
  while (size != 0) {
    if ((*data & 0x80) == 0) {
      ++data;
      --size;
    } else {
      auto [unicode, consumed] = decodeUtf8(data, size);
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
  if (absl::StartsWith(str, "\\u") && str.size() >= 6) {
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

// Compares a string that's possibly an escaped unicode, e.g. \u1234, to
// one that is utf8-encoded.
bool compareUnicodeEscapeAgainstUtf8(absl::string_view& escaped, absl::string_view& utf8) {
  uint32_t escaped_unicode;
  if (parseUnicode(escaped, escaped_unicode)) {
    // If one side of the comparison is a unicode escape,
    auto [unicode, consumed] = decodeUtf8(utf8);
    if (consumed != 0 && unicode == escaped_unicode) {
      utf8 = utf8.substr(consumed, utf8.size() - consumed);
      escaped = escaped.substr(6, escaped.size() - 6);
      return true;
    }
  }
  return false;
}

// Determines whether two strings differ only in whether they have
// literal utf-8 or escaped 3-byte unicode. We do this equivalence
// comparison to enable differential fuzzing between JsonSanitizer and
// protobuf json serialization. The protobuf implementation has made
// some hard-to-understand decisions about what to encode via unicode
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
      a = a.substr(1, a.size() - 1);
      b = b.substr(1, b.size() - 1);
    } else if (!compareUnicodeEscapeAgainstUtf8(a, b) && !compareUnicodeEscapeAgainstUtf8(b, a)) {
      diffs = absl::StrFormat("%s != %s, [%d]%c(0x02%x, \\%03o) != [%d] %c(0x02%x, \\%03o)", all_a,
                              all_b, a.data() - all_a.data(), a[0], a[0], a[0],
                              b.data() - all_b.data(), b[0], b[0], b[0]);
      return false;
    }
  }
}

UnicodeSizePair decodeUtf8(const uint8_t* bytes, uint32_t size) {
  uint32_t unicode = 0;
  uint32_t consumed = 0;

  // See table in https://en.wikipedia.org/wiki/UTF-8, "Encoding" section.
  //
  // See also https://en.cppreference.com/w/cpp/locale/codecvt_utf8 which is
  // marked as deprecated. There is also support in Windows libraries and Boost,
  // which can be discovered on StackOverflow. I could not find a usable OSS
  // implementation. However it's easily derived from the spec on Wikipedia.
  //
  // Note that the code below could be optimized a bit, e.g. by factoring out
  // repeated lookups of the same index in the bytes array and using SSE
  // instructions for the multi-word bit hacking.
  //
  // See also http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ which might be a lot
  // faster, though less readable. As coded, though, it looks like it would read
  // past the end of the input if the input is malformed.
  if (size >= 1 && (bytes[0] & Utf8_1ByteMask) == Utf8_1BytePattern) {
    unicode = bytes[0] & ~Utf8_1ByteMask;
    consumed = 1;
  } else if (size >= 2 && (bytes[0] & Utf8_2ByteMask) == Utf8_2BytePattern &&
             (bytes[1] & Utf8_ContinueMask) == Utf8_ContinuePattern) {
    unicode = bytes[0] & ~Utf8_2ByteMask;
    unicode = (unicode << Utf8_Shift) | (bytes[1] & ~Utf8_ContinueMask);
    if (unicode < 0x80) {
      return UnicodeSizePair(0, 0);
    }
    consumed = 2;
  } else if (size >= 3 && (bytes[0] & Utf8_3ByteMask) == Utf8_3BytePattern &&
             (bytes[1] & Utf8_ContinueMask) == Utf8_ContinuePattern &&
             (bytes[2] & Utf8_ContinueMask) == Utf8_ContinuePattern) {
    unicode = bytes[0] & ~Utf8_3ByteMask;
    unicode = (unicode << Utf8_Shift) | (bytes[1] & ~Utf8_ContinueMask);
    unicode = (unicode << Utf8_Shift) | (bytes[2] & ~Utf8_ContinueMask);
    if (unicode < 0x800) { // 3-byte starts at 0x800
      return UnicodeSizePair(0, 0);
    }
    consumed = 3;
  } else if (size >= 4 && (bytes[0] & Utf8_4ByteMask) == Utf8_4BytePattern &&
             (bytes[1] & Utf8_ContinueMask) == Utf8_ContinuePattern &&
             (bytes[2] & Utf8_ContinueMask) == Utf8_ContinuePattern &&
             (bytes[3] & Utf8_ContinueMask) == Utf8_ContinuePattern) {
    unicode = bytes[0] & ~Utf8_4ByteMask;
    unicode = (unicode << Utf8_Shift) | (bytes[1] & ~Utf8_ContinueMask);
    unicode = (unicode << Utf8_Shift) | (bytes[2] & ~Utf8_ContinueMask);
    unicode = (unicode << Utf8_Shift) | (bytes[3] & ~Utf8_ContinueMask);

    // 4-byte starts at 0x10000
    //
    // Note from https://en.wikipedia.org/wiki/UTF-8:
    // The earlier RFC2279 allowed UTF-8 encoding through code point 0x7FFFFFF.
    // But the current RFC3629 section 3 limits UTF-8 encoding through code
    // point 0x10FFFF, to match the limits of UTF-16.
    if (unicode < 0x10000 || unicode > 0x10ffff) {
      return UnicodeSizePair(0, 0);
    }
    consumed = 4;
  }
  return UnicodeSizePair(unicode, consumed);
}

} // namespace TestUtil
} // namespace Json
} // namespace Envoy
