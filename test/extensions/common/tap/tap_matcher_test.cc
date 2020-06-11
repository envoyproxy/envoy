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

// Base test class for config parameterized tests.
class TapMatcherGenericBodyConfigTest
    : public TapMatcherTestBase,
      public ::testing::TestWithParam<
          std::tuple<TapMatcherTestBase::Direction, std::tuple<std::vector<std::string>, size_t>>> {
};

class TapMatcherGenericBodyTest
    : public TapMatcherTestBase,
      public ::testing::TestWithParam<
          std::tuple<TapMatcherTestBase::Direction,
                     std::tuple<std::vector<std::string>, std::list<std::list<uint32_t>>,
                                std::pair<bool, bool>>>> {
public:
  TapMatcherGenericBodyTest();

  Buffer::OwnedImpl data_;
  std::vector<std::string> body_parts_;
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

TapMatcherGenericBodyTest::TapMatcherGenericBodyTest() {
  std::string hex;
  body_parts_.push_back("This is generic body matcher test for envoy"); // Index 0
  body_parts_.push_back("proxy used to create and assemble http body"); // Index 1
  body_parts_.push_back("env");                                         // Index 2
  body_parts_.push_back("oyp");                                         // Index 3
  body_parts_.push_back("roxy");                                        // Index 4
  body_parts_.push_back("roxy layer 7");                                // Index 5
  body_parts_.push_back("blah");                                        // Index 6
  hex = "xx";
  unsigned char buf[] = {0xde, 0xad};
  memcpy(const_cast<char*>(hex.data()), buf, 2);
  body_parts_.push_back(hex); // Index 7
  unsigned char buf1[] = {0xbe, 0xef};
  memcpy(const_cast<char*>(hex.data()), buf1, 2);
  body_parts_.push_back(hex); // Index 8
}

// Test the case when hex string is not even number of characters
TEST_F(TapMatcherGenericBodyTest, WrongConfigTest) {
  std::string matcher_yaml = R"EOF(
http_request_generic_body_match:
  patterns:
    - binary_match: 4rdHFh%2
)EOF";
  ASSERT_ANY_THROW(TestUtility::loadFromYaml(matcher_yaml, config_));
}

INSTANTIATE_TEST_SUITE_P(
    TapMatcherGenericBodyTestConfigSuite, TapMatcherGenericBodyConfigTest,
    ::testing::Combine(
        ::testing::Values(TapMatcherTestBase::Direction::Request,
                          TapMatcherTestBase::Direction::Response),
        ::testing::Values(
            // Should match - envoy is in the body
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoy\""}, 5),
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoy\""}, 5))));

// Test different configurations against the body.
// Parameterized test passes various configurations
// which are appended to the yaml string.
TEST_P(TapMatcherGenericBodyTest, GenericBodyTest) {
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
  for (const auto& i : std::get<0>(text_and_result)) {
    matcher_yaml += '\n';
    matcher_yaml += i;
    matcher_yaml += '\n';
  }

  TestUtility::loadFromYaml(matcher_yaml, config_);
  buildMatcher(config_, matchers_);
  EXPECT_EQ(1, matchers_.size());
  statuses_.resize(matchers_.size());
  matchers_[0]->onNewStream(statuses_);

  // Now create data. The data is passed to matcher in several
  // steps to simulate that body was not received in one continuous
  // chunk. Data for each step is reassembled from body_parts_.
  for (const auto& i : std::get<1>(text_and_result)) {
    data_.drain(data_.length());
    for (const auto& j : i) {
      data_.add(body_parts_[j].data(), body_parts_[j].length());
    }

    if (Direction::Request == dir) {
      matchers_[0]->onRequestBody(data_, statuses_);
    } else {
      matchers_[0]->onResponseBody(data_, statuses_);
    }
  }
  const std::pair<bool, bool>& expected = std::get<2>(text_and_result);
  EXPECT_EQ((Matcher::MatchStatus{expected.first, expected.second}),
            matchers_[0]->matchStatus(statuses_));
}

INSTANTIATE_TEST_SUITE_P(
    TapMatcherGenericBodyTestSuite, TapMatcherGenericBodyTest,
    ::testing::Combine(
        ::testing::Values(TapMatcherTestBase::Direction::Request,
                          TapMatcherTestBase::Direction::Response),
        ::testing::Values(
            // SEARCHING FOR SINGLE PATTERN - no limit
            // Should match - there is a single body chunk and envoy is in the body
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoy\""},
                            std::list<std::list<uint32_t>>{{0}}, std::make_pair(true, false)),
            // Should match - single body and `envoyproxy` is there
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoyproxy\""},
                            std::list<std::list<uint32_t>>{{0, 1}}, std::make_pair(true, false)),
            // Should match - 2 body chunks. First contains 'envoy' at the end and the second
            // chunk contains 'proxy' at the beginning.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoyproxy\""},
                            std::list<std::list<uint32_t>>{{0}, {1}}, std::make_pair(true, false)),
            // Should match - 3 body chunks containing string `envoyproxy` when reassembled.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoyproxy\""},
                            std::list<std::list<uint32_t>>{{2}, {3}, {4}},
                            std::make_pair(true, false)),
            // Should match - 3 body chunks containing string ``envoyproxy layer`` when reassembled.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoyproxy\""},
                            std::list<std::list<uint32_t>>{{2}, {3}, {5}},
                            std::make_pair(true, false)),
            // Should match - 4 body chunks The last 3 contain string ``envoyproxy layer`` when
            // reassembled.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoyproxy\""},
                            std::list<std::list<uint32_t>>{{6}, {2}, {3}, {5}},
                            std::make_pair(true, false)),
            // Should match - First few chunks does not match, then 3 reassembled match
            // `envoyproxy`.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoyproxy\""},
                            std::list<std::list<uint32_t>>{{6}, {6}, {6}, {2}, {3}, {5}, {6}},
                            std::make_pair(true, false)),
            // Should match - chunk #7 contains hex '0xdead (3q0= in base64 format)'.
            std::make_tuple(std::vector<std::string>{"    - binary_match: \"3q0=\""},
                            std::list<std::list<uint32_t>>{{6}, {6}, {7}, {6}},
                            std::make_pair(true, false)),
            // Should match - chunk #7 contains 0xdead and chunk 8 contains 0xbeef
            // 0xdeadbeef encoded in base64 format is '3q2+7w=='.
            std::make_tuple(std::vector<std::string>{"    - binary_match: \"3q2+7w==\""},
                            std::list<std::list<uint32_t>>{{6}, {6}, {7}, {8}, {6}},
                            std::make_pair(true, false)),
            // Should NOT match - hex 0xdeed (3u0= in base64 format) is not there
            std::make_tuple(std::vector<std::string>{"    - binary_match: \"3u0=\""},
                            std::list<std::list<uint32_t>>{{6}, {6}, {7}, {8}, {6}},
                            std::make_pair(false, true)),

            // SEARCHING FOR SINGLE PATTERN - with limit
            // Should match - there is a single body chunk and 'This' is within
            // search limit.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"This\"",
                                                     "  bytes_limit: 10"},
                            std::list<std::list<uint32_t>>{{0}}, std::make_pair(true, false)),
            // Should NOT match - there is a single body chunk and envoy is in the body
            // but outside of the limit
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoy\"",
                                                     "  bytes_limit: 10"},
                            std::list<std::list<uint32_t>>{{0}}, std::make_pair(false, false)),
            // Should NOT match - 2 body chunks. First contains 'envoy' at the end and the second
            // chunk contains 'proxy' at the beginning. Search is limited to the first 10 bytes
            //  - 'proxy' in the second chunk should not be found as it is outside of the search
            //  limit.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"proxy\"",
                                                     "  bytes_limit: 10"},
                            std::list<std::list<uint32_t>>{{0}, {1}}, std::make_pair(false, false)),
            // Should match - 2 body chunks. First contains 'envoy' at the end and the second
            // chunk contains 'proxy' at the beginning. 'proxy' is located at bytes 44-48
            // so should be found when search limit is 48.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"proxy\"",
                                                     "  bytes_limit: 48"},
                            std::list<std::list<uint32_t>>{{0}, {1}}, std::make_pair(true, false)),
            // Should NOT match - 2 body chunks. First contains 'envoy' at the end and the second
            // chunk contains 'proxy' at the beginning. 'proxy' is located at bytes 44-48.
            // Search limit is 47 bytes, so the last character of 'proxy' is outside of the search
            // limit.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"proxy\"",
                                                     "  bytes_limit: 47"},
                            std::list<std::list<uint32_t>>{{0}, {1}}, std::make_pair(false, false)),
            // Should match - 2 body chunks. First contains 'envoy' at the end and the second
            // chunk contains 'proxy' at the beginning. 'proxy' is located at bytes 44-48.
            // Search limit is 46 bytes, which is enough to include 'envoypro' in search.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoypro\"",
                                                     "  bytes_limit: 46"},
                            std::list<std::list<uint32_t>>{{0}, {1}}, std::make_pair(true, false)),
            // Should NOT match - 2 body chunks. First contains 'envoy' at the end and the second
            // chunk contains 'proxy' at the beginning. 'proxy' is located at bytes 44-48.
            // Search limit is 45 bytes, so the last character of `envoyproxy` is outside of the
            // search limit.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoypro\"",
                                                     "  bytes_limit: 45"},
                            std::list<std::list<uint32_t>>{{0}, {1}}, std::make_pair(false, false)),

            // SEARCHING FOR MULTIPLE PATTERNS - no limit
            // Should NOT match. None of the patterns is in the body.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"balancer\"",
                                                     "    - string_match: \"error\""},
                            std::list<std::list<uint32_t>>{{0}}, std::make_pair(false, true)),
            // Should NOT match. One pattern is in the body but the second is not.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoy\"",
                                                     "    - string_match: \"error\""},
                            std::list<std::list<uint32_t>>{{0}}, std::make_pair(false, true)),
            // Should match. Both patterns are in the body (concatenated frags 0 and 1).
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoy\"",
                                                     "    - string_match: \"proxy\""},
                            std::list<std::list<uint32_t>>{{0, 1}}, std::make_pair(true, false)),
            // SPELLCHECKER(off)
            // Should match. Both patterns should be found. 'envoy' is in the first
            // chunk and '0xbeef' (`vu8=` in base64 format) is in the chunk 8.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoy\"",
                                                     "    - binary_match: \"vu8=\""},
                            std::list<std::list<uint32_t>>{{0, 1}, {8}, {6}},
                            std::make_pair(true, false)),
            // Should match. Both patterns should be found. '0xdeadbeef' is spread
            // across two chunks - 7 and 8. The second pattern 'envoy' is in chunk 0.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoy\"",
                                                     "    - binary_match: \"3q2+7w==\""},
                            std::list<std::list<uint32_t>>{{7}, {8}, {6, 0}},
                            std::make_pair(true, false)),
            // Should match. One pattern is substring of the other and they both
            // are located part in chunk 0 and part in chunk 1.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoyproxy\"",
                                                     "    - string_match: \"voypro\""},
                            std::list<std::list<uint32_t>>{{6}, {0}, {1}, {8}, {6}},
                            std::make_pair(true, false)),
            // Should match. Duplicated pattern which is found in the body.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoyproxy\"",
                                                     "    - string_match: \"envoyproxy\""},
                            std::list<std::list<uint32_t>>{{6}, {0}, {1}, {8}, {6}},
                            std::make_pair(true, false)),

            // SEARCHING FOR MULTIPLE PATTERNS - with limit
            // Should NOT match. None of the patterns is in the body.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"balancer\"",
                                                     "    - string_match: \"error\"",
                                                     "  bytes_limit: 15"},
                            std::list<std::list<uint32_t>>{{0}}, std::make_pair(false, false)),
            // Should NOT match. One pattern is in the body but the second is not.
            // Search limit is large enough to find the first pattern.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoy\"",
                                                     "    - string_match: \"error\"",
                                                     "  bytes_limit: 35"},
                            std::list<std::list<uint32_t>>{{0}}, std::make_pair(false, false)),
            // Should NOT match. One pattern is in the body but the second is not.
            // Search limit is small so none of the patterns should be found.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoy\"",
                                                     "    - string_match: \"error\"",
                                                     "  bytes_limit: 5"},
                            std::list<std::list<uint32_t>>{{0}}, std::make_pair(false, false)),
            // Should NOT match. Both patterns are in the body (concatenated frags 0 and 1).
            // Limit includes only the first pattern.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoy\"",
                                                     "    - string_match: \"proxy\"",
                                                     "  bytes_limit: 30"},
                            std::list<std::list<uint32_t>>{{0, 1}}, std::make_pair(false, false)),
            // Should match. Both patterns should be found. 'envoy' is in the first
            // chunk and '0xbeef (vu8= in base64 format)' is in the chunk 8 and search limit is
            // large enough to include 2 patterns
            std::make_tuple(
                std::vector<std::string>{"    - string_match: \"envoy\"",
                                         "    - binary_match: \"vu8=\"", "  bytes_limit: 90"},
                std::list<std::list<uint32_t>>{{0, 1}, {8}, {6}}, std::make_pair(true, false)),
            // Should match. Both patterns should be found. '0xdeadbeef  (3q2+7w== in base64)' is
            // spread across two chunks - 7 and 8. The second pattern 'envoy' is in chunk 0.
            std::make_tuple(
                std::vector<std::string>{"    - string_match: \"envoy\"",
                                         "    - binary_match: \"3q2+7w==\"", "  bytes_limit: 85"},
                std::list<std::list<uint32_t>>{{7}, {8}, {6, 0}}, std::make_pair(true, false)),
            // Should match. Search limit ends exactly where '0xdeadbeef (3q2+7w== in base64)' ends.
            std::make_tuple(
                std::vector<std::string>{"    - string_match: \"envoy\"",
                                         "    - binary_match: \"3q2+7w==\"", "  bytes_limit: 47"},
                std::list<std::list<uint32_t>>{{0}, {7}, {8}, {6, 0}}, std::make_pair(true, false)),
            // Should NOT match. Search limit ends exactly one byte before end of '0xdeadbeef
            // (3q2+7w== in base64)'.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoy\"",
                                                     "    - binary_match: \"3q2+7w==\"",
                                                     "  bytes_limit: 46"},
                            std::list<std::list<uint32_t>>{{0}, {7}, {8}, {6, 0}},
                            std::make_pair(false, false)),
            // Test the situation when end of the search limit overlaps with end of first chunk.
            // Should NOT match. The second pattern should not be found.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoy\"",
                                                     "    - binary_match: \"3q2+7w==\"",
                                                     "  bytes_limit: 43"},
                            std::list<std::list<uint32_t>>{{0}, {7}, {8}, {6, 0}},
                            std::make_pair(false, false)),

            // SPELLCHECKER(on)
            // Now pass enormously large value. It should work just fine.
            std::make_tuple(std::vector<std::string>{"    - string_match: \"envoy\"",
                                                     "    - binary_match: \"3q2+7w==\"",
                                                     "  bytes_limit: 50000000"},
                            std::list<std::list<uint32_t>>{{0}, {7}, {8}, {6, 0}},
                            std::make_pair(true, false)))));

} // namespace
} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
