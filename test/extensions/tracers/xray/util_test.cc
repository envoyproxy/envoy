#include "extensions/tracers/xray/util.h"

#include "test/test_common/test_time.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

TEST(XRayUtilTest, jsonDocTest) {
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

TEST(XRayUtilTest, wildcardMatchingInvalidArgs) {
  std::string pattern = "";
  std::string text = "whatever";
  EXPECT_FALSE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, wildcardMatchExactPositive) {
  std::string pattern = "foo";
  std::string text = "foo";
  EXPECT_TRUE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, wildcardMatchExactNegative) {
  std::string pattern = "foo";
  std::string text = "bar";
  EXPECT_FALSE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, singleWildcardPositive) {
  std::string pattern = "fo?";
  std::string text = "foo";
  EXPECT_TRUE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, singleWildcardNegative) {
  std::string pattern = "f?o";
  std::string text = "boo";
  EXPECT_FALSE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, multipleWildcardPositive) {
  std::string pattern = "?o?";
  std::string text = "foo";
  EXPECT_TRUE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, multipleWildcardNegative) {
  std::string pattern = "f??";
  std::string text = "boo";
  EXPECT_FALSE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, globPositive) {
  std::string pattern = "*oo";
  std::string text = "foo";
  EXPECT_TRUE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, globPositiveZeroOrMore) {
  std::string pattern = "foo*";
  std::string text = "foo";
  EXPECT_TRUE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, globNegativeZeroOrMore) {
  std::string pattern = "foo*";
  std::string text = "fo0";
  EXPECT_FALSE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, globNegative) {
  std::string pattern = "fo*";
  std::string text = "boo";
  EXPECT_FALSE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, globAndSinglePositive) {
  std::string pattern = "*o?";
  std::string text = "foo";
  EXPECT_TRUE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, globAndSingleNegative) {
  std::string pattern = "f?*";
  std::string text = "boo";
  EXPECT_FALSE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, pureWildcard) {
  std::string pattern = "*";
  std::string text = "foo";
  EXPECT_TRUE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, exactMatch) {
  std::string pattern = "6543210";
  std::string text = "6543210";
  EXPECT_TRUE(Util::wildcardMatch(pattern, text));
}

TEST(XRayUtilTest, misc) {
  std::string animal1 = "?at";
  std::string animal2 = "?o?se";
  std::string animal3 = "*s";

  std::string vehicle1 = "J*";
  std::string vehicle2 = "????";

  std::string match[9] = {"bat",    "cat",  "horse", "mouse", "dogs",
                          "horses", "Jeep", "ford",  "chevy"};
  EXPECT_TRUE(Util::wildcardMatch(animal1, match[0]));
  EXPECT_TRUE(Util::wildcardMatch(animal1, match[1]));
  EXPECT_TRUE(Util::wildcardMatch(animal2, match[2]));
  EXPECT_TRUE(Util::wildcardMatch(animal2, match[3]));
  EXPECT_TRUE(Util::wildcardMatch(animal3, match[4]));
  EXPECT_TRUE(Util::wildcardMatch(animal3, match[5]));

  EXPECT_TRUE(Util::wildcardMatch(vehicle1, match[6]));
  EXPECT_TRUE(Util::wildcardMatch(vehicle2, match[7]));
  EXPECT_FALSE(Util::wildcardMatch(vehicle2, match[8]));

  std::string pattern[4] = {"*", "*/foo", "Foo", "abcd"};
  std::string text[4] = {"cAr", "/bar/foo", "foo", "abc"};
  EXPECT_TRUE(Util::wildcardMatch(pattern[0], text[0]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[1], text[1]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[2], text[2]));
  EXPECT_FALSE(Util::wildcardMatch(pattern[3], text[3]));
}

TEST(XRayUtilTest, edgeCaseGlobs) {
  std::string pattern[8] = {"", "a", "*a", "a*", "a*a", "a*a*", "a*na*ha", "a*b*a*b*a*b*a*b*a*"};
  std::string text[10] = {
      "",
      "a",
      "ba",
      "ab",
      "aa",
      "aba",
      "aaa",
      "aaaaaaaaaaaaaaaaaaaaaaa",
      "anananahahanahana",
      "akljd9gsdfbkjhaabajkhbbyiaahkjbjhbuykjakjhabkjhbabjhkaabbabbaaakljdfsjklababkjbsdabab"};
  EXPECT_TRUE(Util::wildcardMatch(pattern[0], text[0]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[1], text[1]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[2], text[1]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[2], text[2]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[3], text[1]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[3], text[3]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[4], text[4]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[4], text[5]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[4], text[6]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[5], text[4]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[5], text[5]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[5], text[6]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[5], text[7]));
  EXPECT_FALSE(Util::wildcardMatch(pattern[6], text[8]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[7], text[9]));
}

TEST(XRayUtilTest, multiGlobs) {
  std::string pattern[16] = {"*a", "**a", "***a", "**a*", "**a**", "a**b", "?",    "??",
                             "*?", "?*",  "*??",  "?*?",  "*?*",   "*???", "*?*a", "*?*a*"};
  std::string text[6] = {"a", "aa", "aaa", "ab", "ba", "abb"};
  EXPECT_TRUE(Util::wildcardMatch(pattern[0], text[0]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[1], text[0]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[2], text[0]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[3], text[0]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[4], text[0]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[5], text[3]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[5], text[5]));

  EXPECT_TRUE(Util::wildcardMatch(pattern[6], text[0]));
  EXPECT_FALSE(Util::wildcardMatch(pattern[7], text[0]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[8], text[0]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[9], text[1]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[8], text[2]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[9], text[0]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[10], text[1]));
  EXPECT_FALSE(Util::wildcardMatch(pattern[11], text[0]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[11], text[1]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[12], text[0]));
  EXPECT_FALSE(Util::wildcardMatch(pattern[13], text[1]));

  EXPECT_FALSE(Util::wildcardMatch(pattern[14], text[0]));
  EXPECT_TRUE(Util::wildcardMatch(pattern[15], text[4]));
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
