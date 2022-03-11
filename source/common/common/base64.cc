#include "source/common/common/base64.h"

#include <cstdint>
#include <string>

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace {

// clang-format off
constexpr char CHAR_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Conversion table is taken from
// https://opensource.apple.com/source/QuickTimeStreamingServer/QuickTimeStreamingServer-452/CommonUtilitiesLib/base64.c
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

// The base64url tables are copied from above and modified based on table in
// https://tools.ietf.org/html/rfc4648#section-5
constexpr char URL_CHAR_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

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

inline bool decodeBase(const uint8_t cur_char, uint64_t pos, std::string& ret,
                       const unsigned char* const reverse_lookup_table) {
  const unsigned char c = reverse_lookup_table[static_cast<uint32_t>(cur_char)];
  if (c == 64) {
    // Invalid character
    return false;
  }

  switch (pos % 4) {
  case 0:
    ret.push_back(c << 2);
    break;
  case 1:
    ret.back() |= c >> 4;
    ret.push_back(c << 4);
    break;
  case 2:
    ret.back() |= c >> 2;
    ret.push_back(c << 6);
    break;
  case 3:
    ret.back() |= c;
    break;
  }
  return true;
}

inline bool decodeLast(const uint8_t cur_char, uint64_t pos, std::string& ret,
                       const unsigned char* const reverse_lookup_table) {
  const unsigned char c = reverse_lookup_table[static_cast<uint32_t>(cur_char)];
  if (c == 64) {
    // Invalid character
    return false;
  }

  switch (pos % 4) {
  case 0:
    return false;
  case 1:
    ret.back() |= c >> 4;
    return (c & 0b1111) == 0;
  case 2:
    ret.back() |= c >> 2;
    return (c & 0b11) == 0;
  case 3:
    ret.back() |= c;
    break;
  }
  return true;
}

inline void encodeBase(const uint8_t cur_char, uint64_t pos, uint8_t& next_c, std::string& ret,
                       const char* const char_table) {
  switch (pos % 3) {
  case 0:
    ret.push_back(char_table[cur_char >> 2]);
    next_c = (cur_char & 0x03) << 4;
    break;
  case 1:
    ret.push_back(char_table[next_c | (cur_char >> 4)]);
    next_c = (cur_char & 0x0f) << 2;
    break;
  case 2:
    ret.push_back(char_table[next_c | (cur_char >> 6)]);
    ret.push_back(char_table[cur_char & 0x3f]);
    next_c = 0;
    break;
  }
}

inline void encodeLast(uint64_t pos, uint8_t last_char, std::string& ret,
                       const char* const char_table, bool add_padding) {
  switch (pos % 3) {
  case 1:
    ret.push_back(char_table[last_char]);
    if (add_padding) {
      ret.push_back('=');
      ret.push_back('=');
    }
    break;
  case 2:
    ret.push_back(char_table[last_char]);
    if (add_padding) {
      ret.push_back('=');
    }
    break;
  default:
    break;
  }
}

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

  // At most last two chars can be '='.
  size_t n = input.length();
  if (input[n - 1] == '=') {
    n--;
    if (n > 0 && input[n - 1] == '=') {
      n--;
    }
  }
  // Last position before "valid" padding character.
  uint64_t last = n - 1;
  // Determine output length.
  size_t max_length = (n + 3) / 4 * 3;
  if (n % 4 == 3) {
    max_length -= 1;
  }
  if (n % 4 == 2) {
    max_length -= 2;
  }

  std::string ret;
  ret.reserve(max_length);
  for (uint64_t i = 0; i < last; ++i) {
    if (!decodeBase(input[i], i, ret, REVERSE_LOOKUP_TABLE)) {
      return EMPTY_STRING;
    }
  }

  if (!decodeLast(input[last], last, ret, REVERSE_LOOKUP_TABLE)) {
    return EMPTY_STRING;
  }

  ASSERT(ret.size() == max_length);
  return ret;
}

std::string Base64::encode(const Buffer::Instance& buffer, uint64_t length) {
  uint64_t output_length = (std::min(length, buffer.length()) + 2) / 3 * 4;
  std::string ret;
  ret.reserve(output_length);

  uint64_t j = 0;
  uint8_t next_c = 0;
  for (const Buffer::RawSlice& slice : buffer.getRawSlices()) {
    const uint8_t* slice_mem = static_cast<const uint8_t*>(slice.mem_);

    for (uint64_t i = 0; i < slice.len_ && j < length; ++i, ++j) {
      encodeBase(slice_mem[i], j, next_c, ret, CHAR_TABLE);
    }

    if (j == length) {
      break;
    }
  }

  encodeLast(j, next_c, ret, CHAR_TABLE, true);

  return ret;
}

std::string Base64::encode(const char* input, uint64_t length) {
  return encode(input, length, true);
}

std::string Base64::encode(const char* input, uint64_t length, bool add_padding) {
  uint64_t output_length = (length + 2) / 3 * 4;
  std::string ret;
  ret.reserve(output_length);

  uint64_t pos = 0;
  uint8_t next_c = 0;

  for (uint64_t i = 0; i < length; ++i) {
    encodeBase(input[i], pos++, next_c, ret, CHAR_TABLE);
  }

  encodeLast(pos, next_c, ret, CHAR_TABLE, add_padding);

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

  std::string ret;
  ret.reserve(input.length() / 4 * 3 + 3);

  uint64_t last = input.length() - 1;
  for (uint64_t i = 0; i < last; ++i) {
    if (!decodeBase(input[i], i, ret, URL_REVERSE_LOOKUP_TABLE)) {
      return EMPTY_STRING;
    }
  }

  if (!decodeLast(input[last], last, ret, URL_REVERSE_LOOKUP_TABLE)) {
    return EMPTY_STRING;
  }

  return ret;
}

std::string Base64Url::encode(const char* input, uint64_t length) {
  uint64_t output_length = (length + 2) / 3 * 4;
  std::string ret;
  ret.reserve(output_length);

  uint64_t pos = 0;
  uint8_t next_c = 0;

  for (uint64_t i = 0; i < length; ++i) {
    encodeBase(input[i], pos++, next_c, ret, URL_CHAR_TABLE);
  }

  encodeLast(pos, next_c, ret, URL_CHAR_TABLE, false);

  return ret;
}

} // namespace Envoy
