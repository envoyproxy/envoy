#include "base64.h"
#include "empty_string.h"

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

  int max_length = input.length() / 4 * 3;
  // At most last two chars can be '='.
  if (input[input.length() - 1] == '=') {
    max_length--;
    if (input[input.length() - 2] == '=') {
      max_length--;
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
    // Use conversion table to map char to decoded value (value is between 0 and 64).
    result.push_back(REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read])] << 2 |
                     REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read + 1])] >> 4);
    unsigned char c = REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read + 2])];

    // Chars not less than 64 will be skipped, '=' for example.
    // Everything less than 64 is going to be decoded.
    if (c < 64) {
      // Take last 4 bits from 2nd converted char and 4 first bits from 3rd converted char.
      result.push_back(REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read + 1])] << 4 |
                       c >> 2);
      unsigned char d = REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read + 3])];

      if (d < 64) {
        // Take last 2 bits from 3rd converted char and all(6) bits from 4th converted char.
        result.push_back(REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read + 2])] << 6 | d);
      }
    }
    cur_read += 4;
    bytes_left -= 4;
  }

  return result;
}

void Base64::encode_base(const uint8_t cur_char, uint64_t pos, uint8_t& next_c, std::string& ret) {
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

void Base64::encode_last(uint64_t pos, uint8_t last_char, std::string& ret) {
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
  ret.reserve(output_length);
  std::string ret;

  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  buffer.getRawSlices(slices, num_slices);

  uint64_t j = 0;
  uint8_t next_c = 0;
  for (Buffer::RawSlice& slice : slices) {
    const uint8_t* slice_mem = static_cast<const uint8_t*>(slice.mem_);

    for (uint64_t i = 0; i < slice.len_ && j < length; ++i, ++j) {
      encode_base(slice_mem[i], j, next_c, ret);
    }

    if (j == length) {
      break;
    }
  }

  encode_last(j, next_c, ret);

  return ret;
}

std::string Base64::encode(const std::string& input) {
  uint64_t output_length = (input.length() + 2) / 3 * 4;
  std::string ret;
  ret.reserve(output_length);

  uint64_t pos = 0;
  uint8_t next_c = 0;

  for (size_t i = 0; i < input.length(); ++i) {
    encode_base(input[i], pos, next_c, ret);
    pos++;
  }

  encode_last(pos, next_c, ret);

  return ret;
}
