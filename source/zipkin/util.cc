#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/document.h"

#include "util.h"

namespace Zipkin {

void Util::mergeJsons(std::string& target, const std::string& source,
                      const std::string& field_name) {
  rapidjson::Document targetDoc, sourceDoc;
  targetDoc.Parse(target.c_str());
  sourceDoc.Parse(source.c_str());

  targetDoc.AddMember(rapidjson::StringRef(field_name.c_str()), sourceDoc,
                      targetDoc.GetAllocator());

  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> w(sb);
  targetDoc.Accept(w);
  target = sb.GetString();
}

void Util::addArrayToJson(std::string& target, const std::vector<const std::string*>& json_array,
                          const std::string& field_name) {
  std::string stringifiedJsonArray = "[";

  if (json_array.size() > 0) {
    stringifiedJsonArray += *(json_array[0]);
    for (auto it = json_array.begin() + 1; it != json_array.end(); it++) {
      stringifiedJsonArray += ",";
      stringifiedJsonArray += **it;
    }
  }
  stringifiedJsonArray += "]";

  mergeJsons(target, stringifiedJsonArray, field_name);
}

uint64_t Util::timeSinceEpochMicro() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch()).count();
}

uint64_t Util::timeSinceEpochNano() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch()).count();
}

uint64_t Util::generateRandom64() {
  uint64_t seed = timeSinceEpochNano();
  std::mt19937_64 rand_64(seed);
  return rand_64();
}

std::string Util::uint64ToBase16(uint64_t value) {
  std::stringstream stream;
  stream << std::setfill('0') << std::setw(16) << std::hex << value;
  return stream.str();
}
} // Zipkin
