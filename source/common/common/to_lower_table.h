#pragma once

#include <array>
#include <string>

namespace Envoy {
/**
 * Convenience class for converting ASCII strings to lower case using a lookup table for maximum
 * speed.
 */
class ToLowerTable {
public:
  ToLowerTable();

  /**
   * Convert a string to lower case.
   * @param buffer supplies the start of the string.
   * @param size supplies the size of the string.
   */
  void toLowerCase(char* buffer, uint32_t size) const;

  /**
   * Convert a string to lower case.
   * @param supplies the string to convert.
   */
  void toLowerCase(std::string& string) const { toLowerCase(&string[0], string.size()); }

private:
  std::array<uint8_t, 256> table_;
};
} // namespace Envoy
