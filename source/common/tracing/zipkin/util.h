#pragma once

#include <string>
#include <vector>

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
   * @param json_array Vector of string pointers, where each element references a stringified JSON.
   * @param field_name The key name (added to target's JSON) whose value will be a stringified.
   * JSON array derived from json_array.
   */
  static void addArrayToJson(std::string& target, const std::vector<const std::string*>& json_array,
                             const std::string& field_name);

  // ====
  // Miscellaneous
  // ====

  /**
   * Returns a randomly-generated 64-bit integer number.
   */
  static uint64_t generateRandom64();

  /**
   * Converts the given 64-bit integer into a hexadecimal string.
   *
   * @param value The integer to be converted.
   */
  static std::string uint64ToHex(uint64_t value);

  /**
   *  Extracts the IP address and port from a string of the form "<IP>:<port>".
   *
   *  @param address String of the form "<IP>:<port>".
   *  @param ip It will be assigned the IP part of the address string.
   *  @param port It will be assigned the port part of the address string.
   */
  static void getIPAndPort(const std::string& address, std::string& ip, uint16_t& port);
};
} // Zipkin
