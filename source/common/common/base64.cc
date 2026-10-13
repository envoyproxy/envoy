#include "source/common/common/base64.h"

#include <cstdint>
#include <string>

#include "source/common/common/empty_string.h"

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace {

constexpr unsigned char REVERSE_LOOKUP_TABLE[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64, 64, 0,  1,  2,  3,  4,  5,  6,
    7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};

constexpr unsigned char URL_REVERSE_LOOKUP_TABLE[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64, 64, 0,  1,  2,  3,  4,  5,  6,
    7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 63,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};

std::string decodeHelper(absl::string_view input, const unsigned char* lookup_table,
                         bool (*unescape_fn)(absl::string_view, std::string*)) {
  if (input.empty()) {
    return EMPTY_STRING;
  }

  // Strip up to two trailing '=' characters.
  const size_t n = input.length();
  const size_t count = (input[n - 1] == '=') + (n >= 2 && input[n - 2] == '=');
  const size_t real_chars = n - count;

  // A base64 string with length % 4 == 1 is structurally invalid
  if (real_chars % 4 == 1) {
    return EMPTY_STRING;
  }

  // Validate the trailing bits of the last real character.
  if (real_chars > 0) {
    const uint8_t last_value = lookup_table[static_cast<uint8_t>(input[real_chars - 1])];
    if (last_value == 64) {
      return EMPTY_STRING;
    }
    // Base64 groups input into 4 character blocks. Each character represents 6 bits.
    // A valid encoded string can have trailing characters where the unused lower bits must be 0.
    // This code checks whether the unused bits are 0. If they are not, the input is invalid.
    if (real_chars % 4 == 2 && (last_value & 0x0F) != 0) {
      return EMPTY_STRING;
    }
    if (real_chars % 4 == 3 && (last_value & 0x03) != 0) {
      return EMPTY_STRING;
    }
  }

  std::string ret;
  if (!unescape_fn(input.substr(0, real_chars), &ret)) {
    return EMPTY_STRING;
  }

  // If abseil decodes the wrong number of bytes, we must reject the entire input.
  size_t expected_length = real_chars * 3 / 4;
  if (ret.length() != expected_length) {
    return EMPTY_STRING;
  }

  return ret;
}
} // namespace

std::string Base64::decode(absl::string_view input) {
  if (input.length() % 4) {
    return EMPTY_STRING;
  }
  return decodeWithoutPadding(input);
}

std::string Base64::decodeWithoutPadding(absl::string_view input) {
  return decodeHelper(input, REVERSE_LOOKUP_TABLE, absl::Base64Unescape);
}

std::string Base64::encode(const Buffer::Instance& buffer, uint64_t length) {
  const uint64_t encode_length = std::min(length, buffer.length());
  if (encode_length == 0) {
    return EMPTY_STRING;
  }
  std::string ret;
  const auto slices = buffer.getRawSlices();
  if (slices.size() == 1 && slices[0].len_ >= encode_length) {
    absl::Base64Escape(absl::string_view(static_cast<const char*>(slices[0].mem_), encode_length),
                       &ret);
  } else {
    std::string tmp;
    tmp.resize(encode_length);
    buffer.copyOut(0, encode_length, tmp.data());
    absl::Base64Escape(tmp, &ret);
  }
  return ret;
}

std::string Base64::encode(absl::string_view input) { return encode(input.data(), input.length()); }

std::string Base64::encode(const char* input, uint64_t length) {
  return encode(input, length, true);
}

std::string Base64::encode(const char* input, uint64_t length, bool add_padding) {
  std::string ret;
  absl::Base64Escape(absl::string_view(input, length), &ret);

  if (!add_padding) {
    // Remove trailing '='
    const size_t n = ret.size();
    const size_t count = (n >= 1 && ret[n - 1] == '=') + (n >= 2 && ret[n - 2] == '=');
    ret.resize(n - count);
  }
  return ret;
}

void Base64::completePadding(std::string& encoded) {
  if (encoded.length() % 4 != 0) {
    std::string trailing_padding(4 - encoded.length() % 4, '=');
    absl::StrAppend(&encoded, trailing_padding);
  }
}

std::string Base64Url::decode(absl::string_view input) {
  // URL decoding does not accept any padding
  if (!input.empty()) {
    const uint8_t last_value =
        URL_REVERSE_LOOKUP_TABLE[static_cast<uint8_t>(input[input.size() - 1])];
    if (last_value == 64) {
      return EMPTY_STRING;
    }
  }
  return decodeHelper(input, URL_REVERSE_LOOKUP_TABLE, absl::WebSafeBase64Unescape);
}

std::string Base64Url::encode(const char* input, uint64_t length) {
  std::string ret;
  absl::WebSafeBase64Escape(absl::string_view(input, length), &ret);
  return ret;
}

} // namespace Envoy
