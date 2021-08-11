#include "source/common/common/hex.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/common/exception.h"

#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"

namespace Envoy {
std::string Hex::encode(const uint8_t* data, size_t length) {
  static const char* const digits = "0123456789abcdef";

  std::string ret;
  ret.reserve(length * 2);

  for (size_t i = 0; i < length; i++) {
    uint8_t d = data[i];
    ret.push_back(digits[d >> 4]);
    ret.push_back(digits[d & 0xf]);
  }

  return ret;
}

std::vector<uint8_t> Hex::decode(const std::string& hex_string) {
  if (hex_string.empty() || hex_string.size() % 2 != 0) {
    return {};
  }

  std::vector<uint8_t> segment;
  for (size_t i = 0; i < hex_string.size(); i += 2) {
    std::string hex_byte = hex_string.substr(i, 2);
    uint64_t out;
    if (!StringUtil::atoull(hex_byte.c_str(), out, 16)) {
      return {};
    }

    segment.push_back(out);
  }

  return segment;
}

std::string Hex::uint64ToHex(uint64_t value) {
  std::array<uint8_t, 8> data;

  // This is explicitly done for performance reasons
  data[7] = (value & 0x00000000000000FF);
  data[6] = (value & 0x000000000000FF00) >> 8;
  data[5] = (value & 0x0000000000FF0000) >> 16;
  data[4] = (value & 0x00000000FF000000) >> 24;
  data[3] = (value & 0x000000FF00000000) >> 32;
  data[2] = (value & 0x0000FF0000000000) >> 40;
  data[1] = (value & 0x00FF000000000000) >> 48;
  data[0] = (value & 0xFF00000000000000) >> 56;

  return encode(data.data(), data.size());
}

std::string Hex::uint32ToHex(uint32_t value) {
  std::array<uint8_t, 4> data;

  // This is explicitly done for performance reasons
  // using std::stringstream with std::hex is ~3 orders of magnitude slower
  data[3] = (value & 0x000000FF);
  data[2] = (value & 0x0000FF00) >> 8;
  data[1] = (value & 0x00FF0000) >> 16;
  data[0] = (value & 0xFF000000) >> 24;

  return encode(data.data(), data.size());
}

std::string Hex::uint16ToHex(uint16_t value) {
  std::array<uint8_t, 2> data;

  // This is explicitly done for performance reasons
  // using std::stringstream with std::hex is ~3 orders of magnitude slower.
  data[1] = (value & 0x00FF);
  data[0] = (value & 0xFF00) >> 8;

  return encode(data.data(), data.size());
}
} // namespace Envoy
