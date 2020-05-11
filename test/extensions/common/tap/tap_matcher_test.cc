#include "envoy/config/tap/v3/common.pb.h"

#include "common/protobuf/utility.h"

#include "extensions/common/tap/tap_matcher.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {
namespace {

class TapMatcherTestBase {
public:
  std::vector<MatcherPtr> matchers_;
  Matcher::MatchStatusVector statuses_;
  envoy::config::tap::v3::MatchPredicate config_;

  enum class Direction { Request, Response };
};

class TapMatcherTest : public TapMatcherTestBase, public testing::Test {
public:
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
};

class TapMatcherBodyTest
    : public TapMatcherTestBase,
      public ::testing::TestWithParam<
          std::tuple<TapMatcherTestBase::Direction,
                     std::tuple<std::vector<std::string>, std::pair<bool, bool>>>> {
public:
  void createTestBody();

  Buffer::OwnedImpl data_;
};

TEST_F(TapMatcherTest, Any) {
  const std::string matcher_yaml =
      R"EOF(
any_match: true
)EOF";

  TestUtility::loadFromYaml(matcher_yaml, config_);
  buildMatcher(config_, matchers_);
  EXPECT_EQ(1, matchers_.size());
  statuses_.resize(matchers_.size());
  matchers_[0]->onNewStream(statuses_);
  EXPECT_EQ((Matcher::MatchStatus{true, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpRequestHeaders(request_headers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{true, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpRequestTrailers(request_trailers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{true, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpResponseHeaders(response_headers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{true, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpResponseTrailers(response_trailers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{true, false}), matchers_[0]->matchStatus(statuses_));
}

TEST_F(TapMatcherTest, Not) {
  const std::string matcher_yaml =
      R"EOF(
not_match:
  any_match: true
)EOF";

  TestUtility::loadFromYaml(matcher_yaml, config_);
  buildMatcher(config_, matchers_);
  EXPECT_EQ(2, matchers_.size());
  statuses_.resize(matchers_.size());
  matchers_[0]->onNewStream(statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpRequestHeaders(request_headers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpRequestTrailers(request_trailers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpResponseHeaders(response_headers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpResponseTrailers(response_trailers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, false}), matchers_[0]->matchStatus(statuses_));
}

TEST_F(TapMatcherTest, AndMightChangeStatus) {
  const std::string matcher_yaml =
      R"EOF(
and_match:
  rules:
    - http_response_headers_match:
        headers:
          - name: bar
            exact_match: baz
)EOF";

  TestUtility::loadFromYaml(matcher_yaml, config_);
  buildMatcher(config_, matchers_);
  EXPECT_EQ(2, matchers_.size());
  statuses_.resize(matchers_.size());
  matchers_[0]->onNewStream(statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, true}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpRequestHeaders(request_headers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, true}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpRequestTrailers(request_trailers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, true}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpResponseHeaders(response_headers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpResponseTrailers(response_trailers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, false}), matchers_[0]->matchStatus(statuses_));
}

// Method creates a body with the following patterns in it:
// - string "envoy"
// - string "proxy"
// - hex string "BEEFA"
void TapMatcherBodyTest::createTestBody() {
  data_.drain(data_.length());
  std::string body = " This is test body which contains string ";
  body += "envoy";
  body += "layer 7 proxy.";
  body += "Here we throw in a hex value: ";
  data_.add(body.data(), body.length());
  // add hex
  unsigned char buf[] = {0xbe, 0xef, 0xae};
  data_.add(buf, 3);
  std::string body_end = "And his is the end";
  data_.add(body_end.data(), body_end.length());
}

// Test different configurations against the body.
// Parameterized test passes various configurations
// which are appended to the yaml string.
TEST_P(TapMatcherBodyTest, GenericBodyTest) {
  Direction dir = std::get<0>(GetParam());
  std::string matcher_yaml;
  if (Direction::Request == dir) {
    matcher_yaml =
        R"EOF(http_request_generic_body_match:
  patterns:)EOF";
  } else {
    matcher_yaml =
        R"EOF(http_response_generic_body_match:
  patterns:)EOF";
  }

  auto text_and_result = std::get<1>(GetParam());
  // Append vector of matchers
  for (auto i : std::get<0>(text_and_result)) {
    matcher_yaml += '\n';
    matcher_yaml += i;
    matcher_yaml += '\n';
  }

  TestUtility::loadFromYaml(matcher_yaml, config_);
  buildMatcher(config_, matchers_);
  EXPECT_EQ(1, matchers_.size());
  statuses_.resize(matchers_.size());
  matchers_[0]->onNewStream(statuses_);

  createTestBody();

  if (Direction::Request == dir) {
    matchers_[0]->onRequestBody(data_, statuses_);
  } else {
    matchers_[0]->onResponseBody(data_, statuses_);
  }
  const std::pair<bool, bool>& expected = std::get<1>(text_and_result);
  EXPECT_EQ((Matcher::MatchStatus{expected.first, expected.second}),
            matchers_[0]->matchStatus(statuses_));
}

INSTANTIATE_TEST_SUITE_P(
    TapMatcherBodyTestSuite, TapMatcherBodyTest,
    ::testing::Combine(
        ::testing::Values(TapMatcherBodyTest::Direction::Request,
                          TapMatcherBodyTest::Direction::Response),
        ::testing::Values(
            // Should match - envoy is in the body
            std::make_tuple(std::vector<std::string>{"    - contains_text: \"envoy\""},
                            std::make_pair(true, false)),
            // Should not  match - envoy123 is not in the body
            std::make_tuple(std::vector<std::string>{"    - contains_text: \"envoy123\""},
                            std::make_pair(false, false)),
            // Should match - both envoy and proxy are in the body
            std::make_tuple(std::vector<std::string>{"    - contains_text: \"envoy\"",
                                                     "    - contains_text: \"proxy\""},
                            std::make_pair(true, false)),
            // Should not match - envoy is in the body but balancer is not
            std::make_tuple(std::vector<std::string>{"    - contains_text: \"envoy\"",
                                                     "    - contains_text: \"balancer\""},
                            std::make_pair(false, false)),
            // Should match - hex "beef" is in the body
            std::make_tuple(std::vector<std::string>{"    - contains_hex: \"beef\""},
                            std::make_pair(true, false)),
            // Should not match - hex "beefab" is not in the body
            std::make_tuple(std::vector<std::string>{"    - contains_hex: \"beefab\""},
                            std::make_pair(false, false)),
            // Should match - string envoy and hex "beef" are in the body
            std::make_tuple(std::vector<std::string>{"    - contains_hex: \"beef\"",
                                                     "    - contains_text: \"envoy\""},
                            std::make_pair(true, false)),
            // Should not match - string envoy is in the body but and hex "beefab" is not
            std::make_tuple(std::vector<std::string>{"    - contains_hex: \"beefab\"",
                                                     "    - contains_text: \"envoy\""},
                            std::make_pair(false, false)),
            // Should not match - string envoy123 is not in the body and hex "beef" is in the body
            std::make_tuple(std::vector<std::string>{"    - contains_hex: \"beef\"",
                                                     "    - contains_text: \"envoy123\""},
                            std::make_pair(false, false)),
            // Should not match - string and hex are not in the body
            std::make_tuple(std::vector<std::string>{"    - contains_hex: \"beefab\"",
                                                     "    - contains_text: \"envoy123\""},
                            std::make_pair(false, false)))));

} // namespace
} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
