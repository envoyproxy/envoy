#include "common/tracing/zipkin/util.h"

#include <chrono>
#include <random>
#include <regex>

#include "common/common/hex.h"
#include "common/common/utility.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace Envoy {
// TODO(fabolive): Need to add interfaces to the JSON namespace

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

void Util::addArrayToJson(std::string& target, const std::vector<std::string>& json_array,
                          const std::string& field_name) {
  std::string stringified_json_array = "[";

  if (json_array.size() > 0) {
    stringified_json_array += json_array[0];
    for (auto it = json_array.begin() + 1; it != json_array.end(); it++) {
      stringified_json_array += ",";
      stringified_json_array += *it;
    }
  }
  stringified_json_array += "]";

  mergeJsons(target, stringified_json_array, field_name);
}

uint64_t Util::generateRandom64() {
  uint64_t seed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      ProdSystemTimeSource::instance_.currentTime().time_since_epoch())
                      .count();
  std::mt19937_64 rand_64(seed);
  return rand_64();
}
} // namespace Zipkin
} // namespace Envoy
