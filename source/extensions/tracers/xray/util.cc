#include "extensions/tracers/xray/util.h"

#include <chrono>
#include <random>
#include <regex>

#include "common/common/hex.h"
#include "common/common/utility.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {
                std::string Util::mergeKeyWithValue(const std::string& source, const std::string& field_name) {
                    rapidjson::StringBuffer sb;
                    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
                    w.StartObject();
                    w.Key(field_name.c_str());
                    w.String(source.c_str());
                    w.EndObject();
                    std::string new_string = sb.GetString();
                    return new_string;
                }

                void Util::mergeJsons(std::string& target, const std::string& source, const std::string& field_name) {
                    rapidjson::Document target_doc, source_doc(&target_doc.GetAllocator());
                    target_doc.Parse(target.c_str());
                    source_doc.Parse(source.c_str());
                    target_doc.AddMember(rapidjson::StringRef(field_name.c_str()), source_doc, target_doc.GetAllocator());

                    rapidjson::StringBuffer sb;
                    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
                    target_doc.Accept(w);
                    target = sb.GetString();
                }

                std::string Util::addArrayToJsonWithKey(const std::vector<std::string>& json_array, const std::string& field_name) {
                    std::string stringified_json_array;

                    if (json_array.size() > 0) {
                        stringified_json_array += json_array[0];
                        for (auto it = json_array.begin() + 1; it != json_array.end(); it++) {
                            stringified_json_array += ",";
                            stringified_json_array += *it;
                        }
                    }

                    return mergeKeyWithValue(stringified_json_array, field_name);
                }

            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy
