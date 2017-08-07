#include "common/common/base64.h"

#include <cstdint>
#include <string>

#include "common/common/empty_string.h"

namespace Envoy {
static constexpr char CHAR_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Conversion table is taken from
// https://opensource.apple.com/source/QuickTimeStreamingServer/QuickTimeStreamingServer-452/CommonUtilitiesLib/base64.c
static const unsigned char REVERSE_LOOKUP_TABLE[256] = {
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

std::string Base64::decode(const std::string& input) {
  if (input.length() % 4 || input.empty()) {
    return EMPTY_STRING;
  }

  // First position of "valid" padding character.
  uint64_t first_padding_index = input.length();
  int max_length = input.length() / 4 * 3;
  // At most last two chars can be '='.
  if (input[input.length() - 1] == '=') {
    max_length--;
    first_padding_index = input.length() - 1;
    if (input[input.length() - 2] == '=') {
      max_length--;
      first_padding_index = input.length() - 2;
    }
  }
  std::string result;
  result.reserve(max_length);

  uint64_t bytes_left = input.length();
  uint64_t cur_read = 0;

  // Read input string by group of 4 chars, length of input string must be divided evenly by 4.
  // Decode 4 chars 6 bits each into 3 chars 8 bits each.
  while (bytes_left > 0) {
    // Take first 6 bits from 1st converted char and first 2 bits from 2nd converted char,
    // make 8 bits char from it.
    // Use conversion table to map char to decoded value (value is between 0 and 63 inclusive for a
    // valid character).
    const unsigned char a = REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read])];
    const unsigned char b = REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read + 1])];
    if (a == 64 || b == 64) {
      // Input contains an invalid character.
      return EMPTY_STRING;
    }
    result.push_back(a << 2 | b >> 4);
    const unsigned char c = REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read + 2])];

    // Decoded value 64 means invalid character unless we already know it is a valid padding.
    // If so, following characters are all padding.
    // Also we should check there are no unused bits.
    if (c == 64) {
      if (first_padding_index != cur_read + 2) {
        // Input contains an invalid character.
        return EMPTY_STRING;
      } else if (b & 0b1111) {
        // There are unused bits at tail.
        return EMPTY_STRING;
      } else {
        return result;
      }
    }
    // Take last 4 bits from 2nd converted char and 4 first bits from 3rd converted char.
    result.push_back(b << 4 | c >> 2);

    const unsigned char d = REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read + 3])];
    if (d == 64) {
      if (first_padding_index != cur_read + 3) {
        // Input contains an invalid character.
        return EMPTY_STRING;
      } else if (c & 0b11) {
        // There are unused bits at tail.
        return EMPTY_STRING;
      } else {
        return result;
      }
    }
    // Take last 2 bits from 3rd converted char and all(6) bits from 4th converted char.
    result.push_back(c << 6 | d);

    cur_read += 4;
    bytes_left -= 4;
  }

  return result;
}

void Base64::encodeBase(const uint8_t cur_char, uint64_t pos, uint8_t& next_c, std::string& ret) {
  switch (pos % 3) {
  case 0:
    ret.push_back(CHAR_TABLE[cur_char >> 2]);
    next_c = (cur_char & 0x03) << 4;
    break;
  case 1:
    ret.push_back(CHAR_TABLE[next_c | (cur_char >> 4)]);
    next_c = (cur_char & 0x0f) << 2;
    break;
  case 2:
    ret.push_back(CHAR_TABLE[next_c | (cur_char >> 6)]);
    ret.push_back(CHAR_TABLE[cur_char & 0x3f]);
    next_c = 0;
    break;
  }
}

void Base64::encodeLast(uint64_t pos, uint8_t last_char, std::string& ret) {
  switch (pos % 3) {
  case 1:
    ret.push_back(CHAR_TABLE[last_char]);
    ret.push_back('=');
    ret.push_back('=');
    break;
  case 2:
    ret.push_back(CHAR_TABLE[last_char]);
    ret.push_back('=');
    break;
  default:
    break;
  }
}

std::string Base64::encode(const Buffer::Instance& buffer, uint64_t length) {
  uint64_t output_length = (std::min(length, buffer.length()) + 2) / 3 * 4;
  std::string ret;
  ret.reserve(output_length);

  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  buffer.getRawSlices(slices, num_slices);

  uint64_t j = 0;
  uint8_t next_c = 0;
  for (Buffer::RawSlice& slice : slices) {
    const uint8_t* slice_mem = static_cast<const uint8_t*>(slice.mem_);

    for (uint64_t i = 0; i < slice.len_ && j < length; ++i, ++j) {
      encodeBase(slice_mem[i], j, next_c, ret);
    }

    if (j == length) {
      break;
    }
  }

  encodeLast(j, next_c, ret);

  return ret;
}

std::string Base64::encode(const char* input, uint64_t length) {
  uint64_t output_length = (length + 2) / 3 * 4;
  std::string ret;
  ret.reserve(output_length);

  uint64_t pos = 0;
  uint8_t next_c = 0;

  for (uint64_t i = 0; i < length; ++i) {
    encodeBase(input[i], pos++, next_c, ret);
  }

  encodeLast(pos, next_c, ret);

  return ret;
}
} // namespace Envoy
