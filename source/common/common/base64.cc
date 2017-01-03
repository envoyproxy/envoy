#include "base64.h"

static constexpr char CHAR_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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
