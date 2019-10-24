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
  TestUtility::loadFromYaml(yaml, header_matcher);
  return header_matcher;
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

TEST(HeaderDataConstructorTest, PrefixMatchSpecifier) {
  const std::string yaml = R"EOF(
name: test-header
prefix_match: value
  )EOF";

  HeaderUtility::HeaderData header_data =
      HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml));

  EXPECT_EQ("test-header", header_data.name_.get());
  EXPECT_EQ(HeaderUtility::HeaderMatchType::Prefix, header_data.header_match_type_);
  EXPECT_EQ("value", header_data.value_);
}

TEST(HeaderDataConstructorTest, SuffixMatchSpecifier) {
  const std::string yaml = R"EOF(
name: test-header
suffix_match: value
  )EOF";

  HeaderUtility::HeaderData header_data =
      HeaderUtility::HeaderData(parseHeaderMatcherFromYaml(yaml));

  EXPECT_EQ("test-header", header_data.name_.get());
  EXPECT_EQ(HeaderUtility::HeaderMatchType::Suffix, header_data.header_match_type_);
  EXPECT_EQ("value", header_data.value_);
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

TEST(HeaderDataConstructorTest, GetAllOfHeader) {
  TestHeaderMapImpl headers{{"foo", "val1"}, {"bar", "bar2"}, {"foo", "eep, bar"}, {"foo", ""}};

  std::vector<absl::string_view> foo_out;
  Http::HeaderUtility::getAllOfHeader(headers, "foo", foo_out);
  ASSERT_EQ(foo_out.size(), 3);
  ASSERT_EQ(foo_out[0], "val1");
  ASSERT_EQ(foo_out[1], "eep, bar");
  ASSERT_EQ(foo_out[2], "");

  std::vector<absl::string_view> bar_out;
  Http::HeaderUtility::getAllOfHeader(headers, "bar", bar_out);
  ASSERT_EQ(bar_out.size(), 1);
  ASSERT_EQ(bar_out[0], "bar2");

  std::vector<absl::string_view> eep_out;
  Http::HeaderUtility::getAllOfHeader(headers, "eep", eep_out);
  ASSERT_EQ(eep_out.size(), 0);
}

TEST(MatchHeadersTest, MayMatchOneOrMoreRequestHeader) {
  TestHeaderMapImpl headers{{"some-header", "a"}, {"other-header", "b"}};

  const std::string yaml = R"EOF(
name: match-header
regex_match: (a|b)
  )EOF";

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
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

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yamlA)));
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yamlB)));
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

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
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

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
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

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
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

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

TEST(MatchHeadersTest, HeaderSafeRegexMatch) {
  TestHeaderMapImpl matching_headers{{"match-header", "123"}};
  TestHeaderMapImpl unmatching_headers{{"match-header", "1234"}, {"match-header", "123.456"}};
  const std::string yaml = R"EOF(
name: match-header
safe_regex_match:
  google_re2: {}
  regex: \d{3}
  )EOF";

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
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

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
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

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
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

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
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

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
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

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

TEST(MatchHeadersTest, HeaderPrefixMatch) {
  TestHeaderMapImpl matching_headers{{"match-header", "value123"}};
  TestHeaderMapImpl unmatching_headers{{"match-header", "123value"}};

  const std::string yaml = R"EOF(
name: match-header
prefix_match: value
  )EOF";

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

TEST(MatchHeadersTest, HeaderPrefixInverseMatch) {
  TestHeaderMapImpl unmatching_headers{{"match-header", "value123"}};
  TestHeaderMapImpl matching_headers{{"match-header", "123value"}};

  const std::string yaml = R"EOF(
name: match-header
prefix_match: value
invert_match: true
  )EOF";

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

TEST(MatchHeadersTest, HeaderSuffixMatch) {
  TestHeaderMapImpl matching_headers{{"match-header", "123value"}};
  TestHeaderMapImpl unmatching_headers{{"match-header", "value123"}};

  const std::string yaml = R"EOF(
name: match-header
suffix_match: value
  )EOF";

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

TEST(MatchHeadersTest, HeaderSuffixInverseMatch) {
  TestHeaderMapImpl unmatching_headers{{"match-header", "123value"}};
  TestHeaderMapImpl matching_headers{{"match-header", "value123"}};

  const std::string yaml = R"EOF(
name: match-header
suffix_match: value
invert_match: true
  )EOF";

  std::vector<HeaderUtility::HeaderDataPtr> header_data;
  header_data.push_back(
      std::make_unique<HeaderUtility::HeaderData>(parseHeaderMatcherFromYaml(yaml)));
  EXPECT_TRUE(HeaderUtility::matchHeaders(matching_headers, header_data));
  EXPECT_FALSE(HeaderUtility::matchHeaders(unmatching_headers, header_data));
}

TEST(HeaderIsValidTest, InvalidHeaderValuesAreRejected) {
  // ASCII values 1-31 are control characters (with the exception of ASCII
  // values 9, 10, and 13 which are a horizontal tab, line feed, and carriage
  // return, respectively), and are not valid in an HTTP header, per
  // RFC 7230, section 3.2
  for (uint i = 0; i < 32; i++) {
    if (i == 9) {
      continue;
    }

    EXPECT_FALSE(HeaderUtility::headerIsValid(std::string(1, i)));
  }
}

TEST(HeaderIsValidTest, ValidHeaderValuesAreAccepted) {
  EXPECT_TRUE(HeaderUtility::headerIsValid("some-value"));
  EXPECT_TRUE(HeaderUtility::headerIsValid("Some Other Value"));
}

TEST(HeaderAddTest, HeaderAdd) {
  TestHeaderMapImpl headers{{"myheader1", "123value"}};
  TestHeaderMapImpl headers_to_add{{"myheader2", "456value"}};

  HeaderUtility::addHeaders(headers, headers_to_add);

  headers_to_add.iterate(
      [](const Http::HeaderEntry& entry, void* context) -> Http::HeaderMap::Iterate {
        TestHeaderMapImpl* headers = static_cast<TestHeaderMapImpl*>(context);
        Http::LowerCaseString lower_key{std::string(entry.key().getStringView())};
        EXPECT_EQ(entry.value().getStringView(), headers->get(lower_key)->value().getStringView());
        return Http::HeaderMap::Iterate::Continue;
      },
      &headers);
}

} // namespace Http
} // namespace Envoy
