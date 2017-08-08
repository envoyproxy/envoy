#include "common/tracing/zipkin/util.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Zipkin {

TEST(ZipkinUtilTest, utilTests) {
  EXPECT_EQ(typeid(uint64_t).name(), typeid(Util::generateRandom64()).name());

  // Test JSON merging

  std::string merged_json = "{}";
  std::string source_json = "{\"field1\":\"val1\"}";
  Util::mergeJsons(merged_json, source_json, "sub_json");
  std::string expected_json = "{\"sub_json\":{\"field1\":\"val1\"}}";
  EXPECT_EQ(expected_json, merged_json);

  Util::mergeJsons(merged_json, merged_json, "second_merge");
  expected_json =
      "{\"sub_json\":{\"field1\":\"val1\"},\"second_merge\":{\"sub_json\":{\"field1\":\"val1\"}}}";
  EXPECT_EQ(expected_json, merged_json);

  // Test adding an array to a JSON

  std::vector<std::string> json_array;
  Util::addArrayToJson(merged_json, json_array, "array_field");
  expected_json = "{\"sub_json\":{\"field1\":\"val1\"},\"second_merge\":{\"sub_json\":{\"field1\":"
                  "\"val1\"}},\"array_field\":[]}";
  EXPECT_EQ(expected_json, merged_json);

  std::string str1 = "{\"a1\":10}";
  std::string str2 = "{\"a2\":\"10\"}";
  json_array.push_back(str1);
  json_array.push_back(str2);
  Util::addArrayToJson(merged_json, json_array, "second_array");
  expected_json = "{\"sub_json\":{\"field1\":\"val1\"},\"second_merge\":{\"sub_json\":{\"field1\":"
                  "\"val1\"}},\"array_field\":[],\"second_array\":[{\"a1\":10},{\"a2\":\"10\"}]}";
  EXPECT_EQ(expected_json, merged_json);
}
} // namespace Zipkin
} // namespace Envoy
