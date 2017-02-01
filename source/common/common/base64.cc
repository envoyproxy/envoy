#include "base64.h"

#include "common/common/assert.h"

static constexpr char CHAR_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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
  ASSERT(!(input.length() % 4));

  int max_length = input.length() / 4 * 3;
  std::string result;
  result.reserve(max_length);

  uint64_t bytes_left = input.length();
  uint64_t cur_read = 0;

  while (bytes_left > 0) {
    result.push_back(REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read])] << 2 |
                     REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read + 1])] >> 4);
    unsigned char c = REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read + 2])];
    if (c < 64) {
      result.push_back(REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read + 1])] << 4 |
                       c >> 2);
      unsigned char d = REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read + 3])];

      if (d < 64) {
        result.push_back(REVERSE_LOOKUP_TABLE[static_cast<uint32_t>(input[cur_read + 2])] << 6 | d);
      }
    }
    cur_read += 4;
    bytes_left -= 4;
  }

  return result;
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
      const uint8_t c = slice_mem[i];
      switch (j % 3) {
      case 0:
        ret.push_back(CHAR_TABLE[c >> 2]);
        next_c = (c & 0x03) << 4;
        break;
      case 1:
        ret.push_back(CHAR_TABLE[next_c | (c >> 4)]);
        next_c = (c & 0x0f) << 2;
        break;
      case 2:
        ret.push_back(CHAR_TABLE[next_c | (c >> 6)]);
        ret.push_back(CHAR_TABLE[c & 0x3f]);
        next_c = 0;
        break;
      }
    }

    if (j == length) {
      break;
    }
  }

  switch (j % 3) {
  case 1:
    ret.push_back(CHAR_TABLE[next_c]);
    ret.push_back('=');
    ret.push_back('=');
    break;
  case 2:
    ret.push_back(CHAR_TABLE[next_c]);
    ret.push_back('=');
    break;
  default:
    break;
  }

  return ret;
}
