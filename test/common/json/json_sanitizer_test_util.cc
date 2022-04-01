#include "test/common/json/json_sanitizer_test_util.h"

#include <string>

#include "source/common/common/utility.h"
#include "source/common/json/json_sanitizer.h"

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"

namespace Envoy {
namespace Json {

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

    // Avoid ranges where the protobuf serialization fails, returning
    // an empty string.
    invalid_3byte_intervals_.insert(0xd800, 0xe000);

    // Avoid unicode ranges generated from 4-byte utf-8 where protobuf
    // serialization generates two small unicode values instead of the correct one.
    // This must be a protobuf serialization issue.
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
      auto [unicode, consumed] = Envoy::Json::JsonSanitizer::decodeUtf8(data, size);
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

// Implements strtol for hex, but accepting a non-nul-terminated string_view,
// and with one branch per character. This can be done with only one branch
// per string if we use a table instaed of a switch statment, and have all
// the non-hex character inputs map to 0x80, and accumulate the OR of all
// mapped values to test after the loop, but that would be harder to read.
//
// It is good for this code to be somewhat faster (ie not create a temp string)
// so that fuzzers can run faster and cover more cases.
//
// If a string-view based hex decoder is useful in production code, this
// could be factored into a decode() variant in source/common/common.hex.cc.
bool parseUnicode(absl::string_view str, uint32_t& hex_value) {
  if (absl::StartsWith(str, "\\u") && str.size() >= 6) {
    hex_value = 0;
    for (char c : str.substr(2, 4)) {
      uint32_t val = 0;
      switch (c) {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          val = c - '0'; break;
        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
          val = c - 'A' + 10; break;
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
          val = c - 'a' + 10; break;
        default: return false;
      }
      hex_value = 16*hex_value + val;
    }
    return true;
  }
  return false;
}

// Compares a string that's possibly an escaped unicode, e.g. \u1234, to
// one that is utf8-encoded.
bool compareUnicodeEscapeAgainstUtf8(absl::string_view& escaped, absl::string_view& utf8) {
  uint32_t escaped_unicode;
  if (utf8.size() >= 3 && parseUnicode(escaped, escaped_unicode)) {
    // If one side of the comparison is a unicode escape,
    auto [unicode, consumed] = Envoy::Json::JsonSanitizer::decodeUtf8(
        reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size());
    if (consumed == 3 && unicode == escaped_unicode) {
      utf8 = utf8.substr(3, utf8.size() - 3);
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
bool utf8Equivalent(absl::string_view a, absl::string_view b) {
  while (true) {
    if (a.empty() && b.empty()) {
      return true;
    } else if (a.empty() || b.empty()) {
      return false;
    } else if (a[0] == b[0]) {
      a = a.substr(1, a.size() - 1);
      b = b.substr(1, b.size() - 1);
    } else if (!compareUnicodeEscapeAgainstUtf8(a, b) &&
               !compareUnicodeEscapeAgainstUtf8(b, a)) {
      return false;
    }
  }
}

} // namespace Json
} // namespace oEnvoy
