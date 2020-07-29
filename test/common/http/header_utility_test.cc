#include <regex>
#include <vector>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/http/protocol.h"
#include "envoy/json/json_object.h"

#include "common/http/header_utility.h"
#include "common/json/json_loader.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

envoy::config::route::v3::HeaderMatcher parseHeaderMatcherFromYaml(const std::string& yaml) {
  envoy::config::route::v3::HeaderMatcher header_matcher;
  TestUtility::loadFromYaml(yaml, header_matcher);
  return header_matcher;
}

class HeaderUtilityTest : public testing::Test {
public:
  const HeaderEntry& hostHeaderEntry(const std::string& host_value, bool set_connect = false) {
    headers_.setHost(host_value);
    if (set_connect) {
      headers_.setMethod(Http::Headers::get().MethodValues.Connect);
    }
    return *headers_.Host();
  }
  TestRequestHeaderMapImpl headers_;
};

// Port's part from host header get removed
TEST_F(HeaderUtilityTest, RemovePortsFromHost) {
  const std::vector<std::pair<std::string, std::string>> host_headers{
      {"localhost", "localhost"},           // w/o port part
      {"localhost:443", "localhost"},       // name w/ port
      {"", ""},                             // empty
      {":443", ""},                         // just port
      {"192.168.1.1", "192.168.1.1"},       // ipv4
      {"192.168.1.1:443", "192.168.1.1"},   // ipv4 w/ port
      {"[fc00::1]:443", "[fc00::1]"},       // ipv6 w/ port
      {"[fc00::1]", "[fc00::1]"},           // ipv6
      {":", ":"},                           // malformed string #1
      {"]:", "]:"},                         // malformed string #2
      {":abc", ":abc"},                     // malformed string #3
      {"localhost:80", "localhost:80"},     // port not matching w/ hostname
      {"192.168.1.1:80", "192.168.1.1:80"}, // port not matching w/ ipv4
      {"[fc00::1]:80", "[fc00::1]:80"}      // port not matching w/ ipv6
  };

  for (const auto& host_pair : host_headers) {
    auto& host_header = hostHeaderEntry(host_pair.first);
    HeaderUtility::stripPortFromHost(headers_, 443);
    EXPECT_EQ(host_header.value().getStringView(), host_pair.second);
  }
}

// Port's part from host header won't be removed if method is "connect"
TEST_F(HeaderUtilityTest, RemovePortsFromHostConnect) {
  const std::vector<std::pair<std::string, std::string>> host_headers{
      {"localhost:443", "localhost:443"},
  };
  for (const auto& host_pair : host_headers) {
    auto& host_header = hostHeaderEntry(host_pair.first, true);
    HeaderUtility::stripPortFromHost(headers_, 443);
    EXPECT_EQ(host_header.value().getStringView(), host_pair.second);
  }
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
  TestRequestHeaderMapImpl headers{
      {"foo", "val1"}, {"bar", "bar2"}, {"foo", "eep, bar"}, {"foo", ""}};

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
  TestRequestHeaderMapImpl headers{{"some-header", "a"}, {"other-header", "b"}};

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
  TestRequestHeaderMapImpl matching_headers_1{{"match-header-A", "1"}, {"match-header-B", "2"}};
  TestRequestHeaderMapImpl matching_headers_2{
      {"match-header-A", "3"}, {"match-header-B", "4"}, {"match-header-C", "5"}};
  TestRequestHeaderMapImpl unmatching_headers_1{{"match-header-A", "6"}};
  TestRequestHeaderMapImpl unmatching_headers_2{{"match-header-B", "7"}};
  TestRequestHeaderMapImpl unmatching_headers_3{{"match-header-A", "8"}, {"match-header-C", "9"}};
  TestRequestHeaderMapImpl unmatching_headers_4{{"match-header-C", "10"}, {"match-header-D", "11"}};

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
  TestRequestHeaderMapImpl matching_headers{{"match-header", "value"}};
  TestRequestHeaderMapImpl unmatching_headers{{"other-header", "value"}};
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
  TestRequestHeaderMapImpl matching_headers{{"match-header", "match-value"}};
  TestRequestHeaderMapImpl unmatching_headers{{"match-header", "other-value"},
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
  TestRequestHeaderMapImpl matching_headers{{"match-header", "other-value"},
                                            {"other-header", "match-value"}};
  TestRequestHeaderMapImpl unmatching_headers{{"match-header", "match-value"}};

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
  TestRequestHeaderMapImpl matching_headers{{"match-header", "123"}};
  TestRequestHeaderMapImpl unmatching_headers{{"match-header", "1234"},
                                              {"match-header", "123.456"}};
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
  TestRequestHeaderMapImpl matching_headers{{"match-header", "123"}};
  TestRequestHeaderMapImpl unmatching_headers{{"match-header", "1234"},
                                              {"match-header", "123.456"}};
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
  TestRequestHeaderMapImpl matching_headers{{"match-header", "1234"}, {"match-header", "123.456"}};
  TestRequestHeaderMapImpl unmatching_headers{{"match-header", "123"}};

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
  TestRequestHeaderMapImpl matching_headers{{"match-header", "-1"}};
  TestRequestHeaderMapImpl unmatching_headers{{"match-header", "0"},
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
  TestRequestHeaderMapImpl matching_headers{{"match-header", "0"},
                                            {"match-header", "somestring"},
                                            {"match-header", "10.9"},
                                            {"match-header", "-1somestring"}};
  TestRequestHeaderMapImpl unmatching_headers{{"match-header", "-1"}};

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
  TestRequestHeaderMapImpl matching_headers{{"match-header", "123"}};
  TestRequestHeaderMapImpl unmatching_headers{{"nonmatch-header", "1234"},
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
  TestRequestHeaderMapImpl unmatching_headers{{"match-header", "123"}};
  TestRequestHeaderMapImpl matching_headers{{"nonmatch-header", "1234"},
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
  TestRequestHeaderMapImpl matching_headers{{"match-header", "value123"}};
  TestRequestHeaderMapImpl unmatching_headers{{"match-header", "123value"}};

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
  TestRequestHeaderMapImpl unmatching_headers{{"match-header", "value123"}};
  TestRequestHeaderMapImpl matching_headers{{"match-header", "123value"}};

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
  TestRequestHeaderMapImpl matching_headers{{"match-header", "123value"}};
  TestRequestHeaderMapImpl unmatching_headers{{"match-header", "value123"}};

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
  TestRequestHeaderMapImpl unmatching_headers{{"match-header", "123value"}};
  TestRequestHeaderMapImpl matching_headers{{"match-header", "value123"}};

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
  for (int i = 0; i < 32; i++) {
    if (i == 9) {
      continue;
    }

    EXPECT_FALSE(HeaderUtility::headerValueIsValid(std::string(1, i)));
  }
}

TEST(HeaderIsValidTest, ValidHeaderValuesAreAccepted) {
  EXPECT_TRUE(HeaderUtility::headerValueIsValid("some-value"));
  EXPECT_TRUE(HeaderUtility::headerValueIsValid("Some Other Value"));
}

TEST(HeaderIsValidTest, AuthorityIsValid) {
  EXPECT_TRUE(HeaderUtility::authorityIsValid("strangebutlegal$-%&'"));
  EXPECT_FALSE(HeaderUtility::authorityIsValid("illegal{}"));
}

TEST(HeaderIsValidTest, IsConnect) {
  EXPECT_TRUE(HeaderUtility::isConnect(Http::TestRequestHeaderMapImpl{{":method", "CONNECT"}}));
  EXPECT_FALSE(HeaderUtility::isConnect(Http::TestRequestHeaderMapImpl{{":method", "GET"}}));
  EXPECT_FALSE(HeaderUtility::isConnect(Http::TestRequestHeaderMapImpl{}));
}

TEST(HeaderIsValidTest, IsConnectResponse) {
  RequestHeaderMapPtr connect_request{new TestRequestHeaderMapImpl{{":method", "CONNECT"}}};
  RequestHeaderMapPtr get_request{new TestRequestHeaderMapImpl{{":method", "GET"}}};
  TestResponseHeaderMapImpl success_response{{":status", "200"}};
  TestResponseHeaderMapImpl failure_response{{":status", "500"}};

  EXPECT_TRUE(HeaderUtility::isConnectResponse(connect_request, success_response));
  EXPECT_FALSE(HeaderUtility::isConnectResponse(connect_request, failure_response));
  EXPECT_FALSE(HeaderUtility::isConnectResponse(nullptr, success_response));
  EXPECT_FALSE(HeaderUtility::isConnectResponse(get_request, success_response));
}

TEST(HeaderAddTest, HeaderAdd) {
  TestRequestHeaderMapImpl headers{{"myheader1", "123value"}};
  TestRequestHeaderMapImpl headers_to_add{{"myheader2", "456value"}};

  HeaderUtility::addHeaders(headers, headers_to_add);

  headers_to_add.iterate([&headers](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
    Http::LowerCaseString lower_key{std::string(entry.key().getStringView())};
    EXPECT_EQ(entry.value().getStringView(), headers.get(lower_key)->value().getStringView());
    return Http::HeaderMap::Iterate::Continue;
  });
}

TEST(HeaderIsValidTest, HeaderNameContainsUnderscore) {
  EXPECT_FALSE(HeaderUtility::headerNameContainsUnderscore("cookie"));
  EXPECT_FALSE(HeaderUtility::headerNameContainsUnderscore("x-something"));
  EXPECT_TRUE(HeaderUtility::headerNameContainsUnderscore("_cookie"));
  EXPECT_TRUE(HeaderUtility::headerNameContainsUnderscore("cookie_"));
  EXPECT_TRUE(HeaderUtility::headerNameContainsUnderscore("x_something"));
}

TEST(PercentEncoding, ShouldCloseConnection) {
  EXPECT_TRUE(HeaderUtility::shouldCloseConnection(Protocol::Http10,
                                                   TestRequestHeaderMapImpl{{"foo", "bar"}}));
  EXPECT_FALSE(HeaderUtility::shouldCloseConnection(
      Protocol::Http10, TestRequestHeaderMapImpl{{"connection", "keep-alive"}}));
  EXPECT_FALSE(HeaderUtility::shouldCloseConnection(
      Protocol::Http10, TestRequestHeaderMapImpl{{"connection", "foo, keep-alive"}}));

  EXPECT_FALSE(HeaderUtility::shouldCloseConnection(Protocol::Http11,
                                                    TestRequestHeaderMapImpl{{"foo", "bar"}}));
  EXPECT_TRUE(HeaderUtility::shouldCloseConnection(
      Protocol::Http11, TestRequestHeaderMapImpl{{"connection", "close"}}));
  EXPECT_TRUE(HeaderUtility::shouldCloseConnection(
      Protocol::Http11, TestRequestHeaderMapImpl{{"connection", "te,close"}}));
  EXPECT_TRUE(HeaderUtility::shouldCloseConnection(
      Protocol::Http11, TestRequestHeaderMapImpl{{"proxy-connection", "close"}}));
  EXPECT_TRUE(HeaderUtility::shouldCloseConnection(
      Protocol::Http11, TestRequestHeaderMapImpl{{"proxy-connection", "foo,close"}}));
}

} // namespace Http
} // namespace Envoy
