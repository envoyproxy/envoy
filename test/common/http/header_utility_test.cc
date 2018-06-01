#include <regex>
#include <vector>

#include "envoy/api/v2/route/route.pb.h"
#include "envoy/json/json_object.h"

#include "common/http/header_utility.h"
#include "common/json/json_loader.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

envoy::api::v2::route::HeaderMatcher parseHeaderMatcherFromYaml(const std::string& yaml) {
  envoy::api::v2::route::HeaderMatcher header_matcher;
  MessageUtil::loadFromYaml(yaml, header_matcher);
  return header_matcher;
}

TEST(HeaderDataConstructorTest, JsonConstructor) {
  Json::ObjectSharedPtr json =
      Json::Factory::loadFromString("{\"name\":\"test-header\", \"value\":\"value\"}");

  HeaderUtility::HeaderData header_data = HeaderUtility::HeaderData(*json);

  EXPECT_EQ("test-header", header_data.name_.get());
  EXPECT_EQ(HeaderUtility::HeaderMatchType::Value, header_data.header_match_type_);
  EXPECT_EQ("value", header_data.value_);
}

TEST(HeaderDataConstructorTest, NoSpecifierSet) {
  const std::string yaml = R"EOF(
name: test-header
  )EOF";

  HeaderUtility::HeaderData header_data =
      HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml));

  EXPECT_EQ("test-header", header_data.name_.get());
  EXPECT_EQ(HeaderUtility::HeaderMatchType::Present, header_data.header_match_type_);
}

TEST(HeaderDataConstructorTest, ValueSet) {
  const std::string yaml = R"EOF(
name: test-header
value: value
  )EOF";

  HeaderUtility::HeaderData header_data =
      HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml));

  EXPECT_EQ("test-header", header_data.name_.get());
  EXPECT_EQ(HeaderUtility::HeaderMatchType::Value, header_data.header_match_type_);
  EXPECT_EQ("value", header_data.value_);
}

TEST(HeaderDataConstructorTest, ValueAndRegexFlagSet) {
  const std::string yaml = R"EOF(
name: test-header
value: value
regex: true
  )EOF";

  HeaderUtility::HeaderData header_data =
      HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml));

  EXPECT_EQ("test-header", header_data.name_.get());
  EXPECT_EQ(HeaderUtility::HeaderMatchType::Regex, header_data.header_match_type_);
  EXPECT_EQ("", header_data.value_);
}

TEST(HeaderDataConstructorTest, ExactMatchSpecifier) {
  const std::string yaml = R"EOF(
name: test-header
exact_match: value
  )EOF";

  HeaderUtility::HeaderData header_data =
      HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml));

  EXPECT_EQ("test-header", header_data.name_.get());
  EXPECT_EQ(HeaderUtility::HeaderMatchType::Value, header_data.header_match_type_);
  EXPECT_EQ("value", header_data.value_);
}

TEST(HeaderDataConstructorTest, RegexMatchSpecifier) {
  const std::string yaml = R"EOF(
name: test-header
regex_match: value
  )EOF";

  HeaderUtility::HeaderData header_data =
      HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml));

  EXPECT_EQ("test-header", header_data.name_.get());
  EXPECT_EQ(HeaderUtility::HeaderMatchType::Regex, header_data.header_match_type_);
  EXPECT_EQ("", header_data.value_);
}

TEST(HeaderDataConstructorTest, RangeMatchSpecifier) {
  const std::string yaml = R"EOF(
name: test-header
range_match:
  start: 0
  end: -10
  )EOF";

  HeaderUtility::HeaderData header_data =
      HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml));

  EXPECT_EQ("test-header", header_data.name_.get());
  EXPECT_EQ(HeaderUtility::HeaderMatchType::Range, header_data.header_match_type_);
  EXPECT_EQ("", header_data.value_);
  EXPECT_EQ(0, header_data.range_.start());
  EXPECT_EQ(-10, header_data.range_.end());
}

TEST(HeaderDataConstructorTest, PresentMatchSpecifier) {
  const std::string yaml = R"EOF(
name: test-header
present_match: true
  )EOF";

  HeaderUtility::HeaderData header_data =
      HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml));

  EXPECT_EQ("test-header", header_data.name_.get());
  EXPECT_EQ(HeaderUtility::HeaderMatchType::Present, header_data.header_match_type_);
  EXPECT_EQ("", header_data.value_);
}

TEST(HeaderDataConstructorTest, InvertMatchSpecifier) {
  const std::string yaml = R"EOF(
name: test-header
exact_match: value
invert_match: true
)EOF";

  HeaderUtility::HeaderData header_data =
      HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml));

  EXPECT_EQ("test-header", header_data.name_.get());
  EXPECT_EQ(HeaderUtility::HeaderMatchType::Value, header_data.header_match_type_);
  EXPECT_EQ("value", header_data.value_);
  EXPECT_EQ(true, header_data.invert_match_);
}

TEST(MatchHeadersTest, MayMatchOneOrMoreRequestHeader) {
  TestHeaderMapImpl headers{{"some-header", "a"}, {"other-header", "b"}};

  const std::string yaml = R"EOF(
name: match-header
regex_match: (a|b)
  )EOF";

  std::vector<HeaderUtility::HeaderData> header_data;
  header_data.push_back(HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_FALSE(HeaderUtility::matchHeaders(headers, header_data));

  headers.addCopy("match-header", "a");
  EXPECT_TRUE(HeaderUtility::matchHeaders(headers, header_data));
  headers.addCopy("match-header", "b");
  EXPECT_TRUE(HeaderUtility::matchHeaders(headers, header_data));
}

TEST(MatchHeadersTest, MustMatchAllHeaderData) {
  TestHeaderMapImpl matching_headers_1{{"match-header-A", "1"}, {"match-header-B", "2"}};
  TestHeaderMapImpl matching_headers_2{
      {"match-header-A", "3"}, {"match-header-B", "4"}, {"match-header-C", "5"}};
  TestHeaderMapImpl unmatching_headers_1{{"match-header-A", "6"}};
  TestHeaderMapImpl unmatching_headers_2{{"match-header-B", "7"}};
  TestHeaderMapImpl unmatching_headers_3{{"match-header-A", "8"}, {"match-header-C", "9"}};
  TestHeaderMapImpl unmatching_headers_4{{"match-header-C", "10"}, {"match-header-D", "11"}};

  const std::string yamlA = R"EOF(
name: match-header-A
  )EOF";

  const std::string yamlB = R"EOF(
name: match-header-B
  )EOF";

  std::vector<HeaderUtility::HeaderData> header_data;
  header_data.push_back(HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yamlA)));
  header_data.push_back(HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yamlB)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers_1, header_data));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers_2, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers_1, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers_2, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers_3, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers_4, header_data));
}

TEST(MatchHeadersTest, HeaderPresence) {
  TestHeaderMapImpl matching_headers{{"match-header", "value"}};
  TestHeaderMapImpl unmatching_headers{{"other-header", "value"}};
  const std::string yaml = R"EOF(
name: match-header
  )EOF";

  std::vector<HeaderUtility::HeaderData> header_data;
  header_data.push_back(HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

TEST(MatchHeadersTest, HeaderExactMatch) {
  TestHeaderMapImpl matching_headers{{"match-header", "match-value"}};
  TestHeaderMapImpl unmatching_headers{{"match-header", "other-value"},
                                       {"other-header", "match-value"}};
  const std::string yaml = R"EOF(
name: match-header
exact_match: match-value
  )EOF";

  std::vector<HeaderUtility::HeaderData> header_data;
  header_data.push_back(HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

TEST(MatchHeadersTest, HeaderExactMatchInverse) {
  TestHeaderMapImpl matching_headers{{"match-header", "other-value"},
                                     {"other-header", "match-value"}};
  TestHeaderMapImpl unmatching_headers{{"match-header", "match-value"}};

  const std::string yaml = R"EOF(
name: match-header
exact_match: match-value
invert_match: true
  )EOF";

  std::vector<HeaderUtility::HeaderData> header_data;
  header_data.push_back(HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

TEST(MatchHeadersTest, HeaderRegexMatch) {
  TestHeaderMapImpl matching_headers{{"match-header", "123"}};
  TestHeaderMapImpl unmatching_headers{{"match-header", "1234"}, {"match-header", "123.456"}};
  const std::string yaml = R"EOF(
name: match-header
regex_match: \d{3}
  )EOF";

  std::vector<HeaderUtility::HeaderData> header_data;
  header_data.push_back(HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

TEST(MatchHeadersTest, HeaderRegexInverseMatch) {
  TestHeaderMapImpl matching_headers{{"match-header", "1234"}, {"match-header", "123.456"}};
  TestHeaderMapImpl unmatching_headers{{"match-header", "123"}};

  const std::string yaml = R"EOF(
name: match-header
regex_match: \d{3}
invert_match: true
  )EOF";

  std::vector<HeaderUtility::HeaderData> header_data;
  header_data.push_back(HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

TEST(MatchHeadersTest, HeaderRangeMatch) {
  TestHeaderMapImpl matching_headers{{"match-header", "-1"}};
  TestHeaderMapImpl unmatching_headers{{"match-header", "0"},
                                       {"match-header", "somestring"},
                                       {"match-header", "10.9"},
                                       {"match-header", "-1somestring"}};
  const std::string yaml = R"EOF(
name: match-header
range_match:
  start: -10
  end: 0
  )EOF";

  std::vector<HeaderUtility::HeaderData> header_data;
  header_data.push_back(HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

TEST(MatchHeadersTest, HeaderRangeInverseMatch) {
  TestHeaderMapImpl matching_headers{{"match-header", "0"},
                                     {"match-header", "somestring"},
                                     {"match-header", "10.9"},
                                     {"match-header", "-1somestring"}};
  TestHeaderMapImpl unmatching_headers{{"match-header", "-1"}};

  const std::string yaml = R"EOF(
name: match-header
range_match:
  start: -10
  end: 0
invert_match: true
  )EOF";

  std::vector<HeaderUtility::HeaderData> header_data;
  header_data.push_back(HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

TEST(MatchHeadersTest, HeaderPresentMatch) {
  TestHeaderMapImpl matching_headers{{"match-header", "123"}};
  TestHeaderMapImpl unmatching_headers{{"nonmatch-header", "1234"},
                                       {"other-nonmatch-header", "123.456"}};

  const std::string yaml = R"EOF(
name: match-header
present_match: true
  )EOF";

  std::vector<HeaderUtility::HeaderData> header_data;
  header_data.push_back(HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

TEST(MatchHeadersTest, HeaderPresentInverseMatch) {
  TestHeaderMapImpl unmatching_headers{{"match-header", "123"}};
  TestHeaderMapImpl matching_headers{{"nonmatch-header", "1234"},
                                     {"other-nonmatch-header", "123.456"}};

  const std::string yaml = R"EOF(
name: match-header
present_match: true
invert_match: true
  )EOF";

  std::vector<HeaderUtility::HeaderData> header_data;
  header_data.push_back(HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

} // namespace Http
} // namespace Envoy
