#pragma once

#include <vector>
#include <string>
#include <chrono>

namespace Zipkin {

typedef std::chrono::system_clock Duration;

class Util {
public:
  static void mergeJsons(std::string& target, const std::string& source,
                         const std::string& field_name);

  static void addArrayToJson(std::string& target, const std::vector<const std::string*>& json_array,
                             const std::string& field_name);

  static uint64_t timeSinceEpochMicro();

  static uint64_t timeSinceEpochNano();

  static uint64_t generateRandom64();

  static std::string uint64ToBase16(uint64_t value);
};
} // Zipkin
