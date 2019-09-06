#pragma once

#include <string>
#include <vector>

#include "envoy/common/time.h"

#include "common/common/byte_order.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

/**
 * Utility class with a few convenient methods
 */
class Util {
public:
  // ====
  // Stringified-JSON manipulation
  // ====

  /**
   * Merges the stringified JSONs given in target and source.
   *
   * @param target It will contain the resulting stringified JSON.
   * @param source The stringified JSON that will be added to target.
   * @param field_name The key name (added to target's JSON) whose value will be the JSON in source.
   */
  static void mergeJsons(std::string& target, const std::string& source,
                         const std::string& field_name);

  /**
   * Merges a stringified JSON and a vector of stringified JSONs.
   *
   * @param target It will contain the resulting stringified JSON.
   * @param json_array Vector of strings, where each element is a stringified JSON.
   * @param field_name The key name (added to target's JSON) whose value will be a stringified.
   * JSON array derived from json_array.
   */
  static void addArrayToJson(std::string& target, const std::vector<std::string>& json_array,
                             const std::string& field_name);

  // ====
  // Miscellaneous
  // ====

  /**
   * Returns a randomly-generated 64-bit integer number.
   */
  static uint64_t generateRandom64(TimeSource& time_source);

  /**
   * Returns byte string representation of a number.
   *
   * @param value Number that will be represented in byte string.
   * @return std::string byte string representation of a number.
   */
  template <typename Type> static std::string toByteString(Type value) {
    return std::string(reinterpret_cast<const char*>(&value), sizeof(Type));
  }

  /**
   * Returns big endian byte string representation of a number.
   *
   * @param value Number that will be represented in byte string.
   * @param flip indicates to flip order or not.
   * @return std::string byte string representation of a number.
   */
  template <typename Type> static std::string toBigEndianByteString(Type value) {
    auto bytes = toEndianness<ByteOrder::BigEndian>(value);
    return std::string(reinterpret_cast<const char*>(&bytes), sizeof(Type));
  }
};

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
