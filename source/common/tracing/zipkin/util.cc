#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/document.h"

#include "common/common/hex.h"
#include "common/tracing/zipkin/util.h"

namespace Zipkin {

void Util::mergeJsons(std::string& target, const std::string& source,
                      const std::string& field_name) {
  rapidjson::Document target_doc, source_doc;
  target_doc.Parse(target.c_str());
  source_doc.Parse(source.c_str());

  target_doc.AddMember(rapidjson::StringRef(field_name.c_str()), source_doc,
                       target_doc.GetAllocator());

  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> w(sb);
  target_doc.Accept(w);
  target = sb.GetString();
}

void Util::addArrayToJson(std::string& target, const std::vector<const std::string*>& json_array,
                          const std::string& field_name) {
  std::string stringified_json_array = "[";

  if (json_array.size() > 0) {
    stringified_json_array += *(json_array[0]);
    for (auto it = json_array.begin() + 1; it != json_array.end(); it++) {
      stringified_json_array += ",";
      stringified_json_array += **it;
    }
  }
  stringified_json_array += "]";

  mergeJsons(target, stringified_json_array, field_name);
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

std::string Util::uint64ToHex(uint64_t value) {
  std::vector<uint8_t> data(8);

  data[7] = (value & 0x00000000000000FF);
  data[6] = (value & 0x000000000000FF00) >> 8;
  data[5] = (value & 0x0000000000FF0000) >> 16;
  data[4] = (value & 0x00000000FF000000) >> 24;
  data[3] = (value & 0x000000FF00000000) >> 32;
  data[2] = (value & 0x0000FF0000000000) >> 40;
  data[1] = (value & 0x00FF000000000000) >> 48;
  data[0] = (value & 0xFF00000000000000) >> 56;

  return Hex::encode(&data[0], data.size());
}

void Util::getIPAndPort(const std::string& address, std::string& ip, uint16_t& port) {
  std::regex re("^(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})(:(\\d+))?$");
  std::smatch match;
  if (std::regex_search(address, match, re)) {
    ip = match.str(1);
    if (match.str(3).size() > 0) {
      port = std::stoi(match.str(3));
    }
  }
}
} // Zipkin
