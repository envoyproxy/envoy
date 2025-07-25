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

bool decodeEscapedJson(absl::string_view sanitized, std::string& decoded, std::string& errmsg) {
  while (!sanitized.empty()) {
    uint32_t hex;
    if (sanitized.size() >= UnicodeEscapeLength &&
        parseUnicode(sanitized.substr(0, UnicodeEscapeLength), hex)) {
      if (hex >= 256) {
        errmsg = absl::StrFormat("Unexpected encoding >= 256: %u", hex);
        return false;
      }
      decoded.append(1, hex);
      removePrefix(sanitized, UnicodeEscapeLength);
    } else {
      decoded.append(1, sanitized[0]);
      removePrefix(sanitized, 1);
    }
  }
  return true;
}

} // namespace TestUtil
} // namespace Json
} // namespace Envoy
