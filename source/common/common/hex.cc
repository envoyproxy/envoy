#include "common/common/hex.h"

#include "envoy/common/exception.h"

#include "common/common/utility.h"

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
  if (hex_string.size() == 0 || hex_string.size() % 2 != 0) {
    throw EnvoyException(fmt::format("invalid hex string '{}'", hex_string));
  }

  std::vector<uint8_t> segment;
  for (size_t i = 0; i < hex_string.size(); i += 2) {
    std::string hex_byte = hex_string.substr(i, 2);
    uint64_t out;
    if (!StringUtil::atoul(hex_byte.c_str(), out, 16)) {
      throw EnvoyException(fmt::format("invalid hex string '{}'", hex_string));
    }

    segment.push_back(out);
  }

  return segment;
}
