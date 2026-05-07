#include "source/common/common/base64.h"

#include <cstdint>
#include <string>

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace {
// clang-format off
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

// Conversion table is taken from
// https://opensource.apple.com/source/QuickTimeStreamingServer/QuickTimeStreamingServer-452/CommonUtilitiesLib/base64.c

// The base64url tables are copied from above and modified based on table in
// https://tools.ietf.org/html/rfc4648#section-5

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
// clang-format on

} // namespace

std::string Base64::decode(absl::string_view input) {
  if (input.length() % 4) {
    return EMPTY_STRING;
  }
  return decodeWithoutPadding(input);
}

std::string Base64::decodeWithoutPadding(absl::string_view input) {
  if (input.empty()) {
    return EMPTY_STRING;
  }

  // Strip up to two trailing '=' characters.
  size_t n = input.length();
  size_t count = 0;
  if (input[n - 1] == '=') {
    count++;
    if (n >= 2 && input[n - 2] == '=') {
      count++;
      if (n >= 3 && input[n - 3] == '=') {
        return EMPTY_STRING;
      }
    }
  }

  size_t real_chars = n - count;

  // A base64 string with length % 4 == 1 is structurally invalid
  if (real_chars % 4 == 1) {
    return EMPTY_STRING;
  }

  // Validate the trailing bits of the last real character.
  if (real_chars > 0) {
    uint8_t last_value = REVERSE_LOOKUP_TABLE[static_cast<uint8_t>(input[real_chars - 1])];
    if (last_value == 64) {
      return EMPTY_STRING;
    }
    if (real_chars % 4 == 2 && (last_value & 0x0F) != 0) {
      return EMPTY_STRING;
    }
    if (real_chars % 4 == 3 && (last_value & 0x03) != 0) {
      return EMPTY_STRING;
    }
  }

  std::string ret;
  if (!absl::Base64Unescape(input.substr(0, real_chars), &ret)) {
    return EMPTY_STRING;
  }

  // If abseil decoded fewer bytes than expected, we must reject the entire input.
  size_t expected_length = real_chars * 3 / 4;
  if (ret.length() != expected_length) {
    return EMPTY_STRING;
  }

  return ret;
}

std::string Base64::encode(const Buffer::Instance& buffer, uint64_t length) {
  size_t encode_length = std::min(length, buffer.length());
  std::string ret;
  std::string tmp;
  tmp.resize(encode_length);
  buffer.copyOut(0, encode_length, tmp.data());
  absl::Base64Escape(tmp, &ret);
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
    while (!ret.empty() && ret.back() == '=') {
      ret.pop_back();
    }
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
  if (input.empty()) {
    return EMPTY_STRING;
  }

  // Strip up to two trailing '=' characters.
  size_t n = input.length();
  size_t count = 0;
  if (input[n - 1] == '=') {
    count++;
    if (n >= 2 && input[n - 2] == '=') {
      count++;
      if (n >= 3 && input[n - 3] == '=') {
        return EMPTY_STRING;
      }
    }
  }

  size_t real_chars = n - count;

  // A base64 string with length % 4 == 1 is structurally invalid
  if (real_chars % 4 == 1) {
    return EMPTY_STRING;
  }

  // Validate the trailing bits of the last real character.
  if (real_chars > 0) {
    uint8_t last_value = URL_REVERSE_LOOKUP_TABLE[static_cast<uint8_t>(input[real_chars - 1])];
    if (last_value == 64) {
      return EMPTY_STRING;
    }
    if (real_chars % 4 == 2 && (last_value & 0x0F) != 0) {
      return EMPTY_STRING;
    }
    if (real_chars % 4 == 3 && (last_value & 0x03) != 0) {
      return EMPTY_STRING;
    }
  }

  std::string ret;
  if (!absl::WebSafeBase64Unescape(input.substr(0, real_chars), &ret)) {
    return EMPTY_STRING;
  }

  // If abseil decoded fewer bytes than expected, we must reject the entire input.
  size_t expected_length = real_chars * 3 / 4;
  if (ret.length() != expected_length) {
    return EMPTY_STRING;
  }

  return ret;
}

std::string Base64Url::encode(const char* input, uint64_t length) {
  std::string ret;
  absl::WebSafeBase64Escape(absl::string_view(input, length), &ret);
  return ret;
}

} // namespace Envoy
