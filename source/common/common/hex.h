#pragma once

#include <string>
#include <vector>

namespace Envoy {
/**
 * Hex encoder/decoder. Produces lowercase hex digits. Can consume either lowercase or uppercase
 * digits.
 */
class Hex final {
public:
  /**
   * Generates a hex dump of the given data
   * @param data the binary data to convert
   * @return the hex encoded string representing data
   */
  static std::string encode(const std::vector<uint8_t>& data) {
    return encode(data.data(), data.size());
  }

  /**
   * Generates a hex dump of the given data
   * @param data the binary data to convert
   * @param length the length of the data
   * @return the hex encoded string representing data
   */
  static std::string encode(const uint8_t* data, size_t length);

  /**
   * Converts a hex dump to binary data
   * @param input the hex dump to decode
   * @return binary data or empty vector in case of invalid input
   */
  static std::vector<uint8_t> decode(const std::string& input);

  /**
   * Converts the given 64-bit unsigned integer into a hexadecimal string.
   * The result is always a string of 16 characters left padded with zeroes.
   * @param value The unsigned integer to be converted.
   * @return value as hexadecimal string
   */
  static std::string uint64ToHex(uint64_t value);

  /**
   * Converts the given 32-bit unsigned integer into a hexadecimal string.
   * The result is always a string of 8 characters left padded with zeroes.
   * @param value The unsigned integer to be converted.
   * @return value as hexadecimal string
   */
  static std::string uint32ToHex(uint32_t value);
};
} // namespace Envoy
