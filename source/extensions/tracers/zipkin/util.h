#pragma once

#include <string>
#include <vector>

#include "envoy/common/time.h"

#include "source/common/common/byte_order.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

/**
 * Utility class with a few convenient methods
 */
class Util {
public:
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
    return {reinterpret_cast<const char*>(&value), sizeof(Type)};
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
    return {reinterpret_cast<const char*>(&bytes), sizeof(Type)};
  }

  using Replacements = std::vector<std::pair<const std::string, const std::string>>;

  /**
   * Returns a wrapped uint64_t value as a string. In addition to that, it also pushes back a
   * replacement to the given replacements vector. The replacement includes the supplied name
   * as a key, for identification in a JSON stream.
   *
   * @param value unt64_t number that will be represented in string.
   * @param name std::string that is the key for the value being replaced.
   * @param replacements a container to hold the required replacements when serializing this value.
   * @return ProtobufWkt::Value wrapped uint64_t as a string.
   */
  static ProtobufWkt::Value uint64Value(uint64_t value, absl::string_view name,
                                        Replacements& replacements);
};

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
